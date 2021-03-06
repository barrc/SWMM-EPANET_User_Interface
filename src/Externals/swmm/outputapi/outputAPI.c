/*
* outputAPI.c
*
*      Author: Colleen Barr
*      Modified by: Michael E. Tryby
*
*
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "outputAPI.h"
//#include "datetime.h"


// NOTE: These depend on machine data model and may change when porting
#define F_OFF off_t      // Must be a 8 byte / 64 bit integer for large file support
#define INT4  int        // Must be a 4 byte / 32 bit integer type
#define REAL4 float      // Must be a 4 byte / 32 bit real type

#define RECORDSIZE  4    // Memory alignment 4 byte word size for both int and real
#define DATESIZE    8    // Dates are stored as 8 byte word size

#define MEMCHECK(x)  (((x) == NULL) ? 414 : 0 )

struct IDentry {
	char* IDname;
	int length;
};
typedef struct IDentry idEntry;

//-----------------------------------------------------------------------------
//  Shared variables
//-----------------------------------------------------------------------------

struct SMOutputAPI {
	char name[MAXFILENAME + 1];           // file path/name
	FILE* file;                        // FILE structure pointer

	struct IDentry *elementNames;      // array of pointers to element names

	long Nperiods;                     // number of reporting periods
	int FlowUnits;                     // flow units code

	int Nsubcatch;                     // number of subcatchments
	int Nnodes;                        // number of drainage system nodes
	int Nlinks;                        // number of drainage system links
	int Npolluts;                      // number of pollutants tracked

	int SubcatchVars;                  // number of subcatch reporting variables
	int NodeVars;                      // number of node reporting variables
	int LinkVars;                      // number of link reporting variables
	int SysVars;                       // number of system reporting variables

	double StartDate;                  // start date of simulation
	int    ReportStep;                 // reporting time step (seconds)

	F_OFF IDPos;					   // file position where object ID names start
	F_OFF ObjPropPos;				   // file position where object properties start
	F_OFF ResultsPos;                  // file position where results start
	F_OFF BytesPerPeriod;              // bytes used for results in each period
};

//-----------------------------------------------------------------------------
//   Local functions
//-----------------------------------------------------------------------------
int    validateFile(SMOutputAPI* smoapi);
void   initElementNames(SMOutputAPI* smoapi);

double getTimeValue(SMOutputAPI* smoapi, long timeIndex);
float  getSubcatchValue(SMOutputAPI* smoapi, long timeIndex, int subcatchIndex, SMO_subcatchAttribute attr);
float  getNodeValue(SMOutputAPI* smoapi, long timeIndex, int nodeIndex, SMO_nodeAttribute attr);
float  getLinkValue(SMOutputAPI* smoapi, long timeIndex, int linkIndex, SMO_linkAttribute attr);
float  getSystemValue(SMOutputAPI* smoapi, long timeIndex, SMO_systemAttribute attr);


SMOutputAPI* DLLEXPORT SMO_init(void)
//
//  Purpose: Returns an initialized pointer for the opaque SMOutputAPI
//    structure.
//
{
	SMOutputAPI *smoapi = malloc(sizeof(struct SMOutputAPI));
	smoapi->elementNames = NULL;

	return smoapi;
}

int DLLEXPORT SMO_open(SMOutputAPI* smoapi, const char* path)
//
//  Purpose: Open the output binary file and read epilogue.
//
{
	int version, err, errorcode = 0;
	F_OFF offset;

    if (smoapi == NULL) errorcode = 410;
    else
    {
    	strncpy(smoapi->name, path, MAXFILENAME);

    	// --- open the output file
    	if ((smoapi->file = fopen(path, "rb")) == NULL) errorcode = 434;
    	// --- validate the output file
    	else if ((err = validateFile(smoapi)) != 0) errorcode = err;

    	else {
    		// --- otherwise read additional parameters from start of file
    		fread(&version, RECORDSIZE, 1, smoapi->file);
    		fread(&(smoapi->FlowUnits), RECORDSIZE, 1, smoapi->file);
    		fread(&(smoapi->Nsubcatch), RECORDSIZE, 1, smoapi->file);
    		fread(&(smoapi->Nnodes), RECORDSIZE, 1, smoapi->file);
    		fread(&(smoapi->Nlinks), RECORDSIZE, 1, smoapi->file);
    		fread(&(smoapi->Npolluts), RECORDSIZE, 1, smoapi->file);

    		// Skip over saved subcatch/node/link input values
    		offset = (smoapi->Nsubcatch + 2) * RECORDSIZE  // Subcatchment area
    			+ (3 * smoapi->Nnodes + 4) * RECORDSIZE  // Node type, invert & max depth
				+ (5 * smoapi->Nlinks + 6) * RECORDSIZE; // Link type, z1, z2, max depth & length
    		offset += smoapi->ObjPropPos;

    		// Read number & codes of computed variables
    		fseeko64(smoapi->file, offset, SEEK_SET);
    		fread(&(smoapi->SubcatchVars), RECORDSIZE, 1, smoapi->file); // # Subcatch variables

    		fseeko64(smoapi->file, smoapi->SubcatchVars*RECORDSIZE, SEEK_CUR);
    		fread(&(smoapi->NodeVars), RECORDSIZE, 1, smoapi->file);     // # Node variables

    		fseeko64(smoapi->file, smoapi->NodeVars*RECORDSIZE, SEEK_CUR);
    		fread(&(smoapi->LinkVars), RECORDSIZE, 1, smoapi->file);     // # Link variables

    		fseeko64(smoapi->file, smoapi->LinkVars*RECORDSIZE, SEEK_CUR);
    		fread(&(smoapi->SysVars), RECORDSIZE, 1, smoapi->file);     // # System variables

    		// --- read data just before start of output results
    		offset = smoapi->ResultsPos - 3 * RECORDSIZE;
    		fseeko64(smoapi->file, offset, SEEK_SET);
    		fread(&(smoapi->StartDate), DATESIZE, 1, smoapi->file);
    		fread(&(smoapi->ReportStep), RECORDSIZE, 1, smoapi->file);

    		// --- compute number of bytes of results values used per time period
    		smoapi->BytesPerPeriod = DATESIZE +
    				(smoapi->Nsubcatch*smoapi->SubcatchVars +
    						smoapi->Nnodes*smoapi->NodeVars +
							smoapi->Nlinks*smoapi->LinkVars +
							smoapi->SysVars)*RECORDSIZE;
    	}
    }

	if (errorcode)
		SMO_close(smoapi);

	return errorcode;

}


int DLLEXPORT SMO_getProjectSize(SMOutputAPI* smoapi, SMO_elementCount code, int* count)
//
//   Purpose: Returns project size.
//
{
	int errorcode = 0;

	*count = -1;
    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
	else
	{
		switch (code)
		{
		case subcatchCount:
			*count = smoapi->Nsubcatch;
			break;

		case nodeCount:
			*count = smoapi->Nnodes;
			break;

		case linkCount:
			*count = smoapi->Nlinks;
			break;

		case pollutantCount:
			*count = smoapi->Npolluts;
			break;

		default:
			errorcode = 421;
		}
	}

	return errorcode;
}


int DLLEXPORT SMO_getResultCount(SMOutputAPI* smoapi, SMO_elementType type, int* count)
//
//   Purpose: Returns number of results associated with each element type.
//
{
	int errorcode = 0;

	*count = -1;

	if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
	else
	{
		switch (type)
		{
		case subcatch:
			*count = smoapi->SubcatchVars;
			break;

		case node:
			*count = smoapi->NodeVars;
			break;

		case link:
			*count = smoapi->LinkVars;
			break;

		case sys:
			*count = smoapi->SysVars;
			break;

		default:
			errorcode = 421;
		}
	}

	return errorcode;
}


int DLLEXPORT SMO_getUnits(SMOutputAPI* smoapi, SMO_unit code, int* unitFlag)
//
//   Purpose: Returns flow rate units.
//
//	 Note:    Concentration units are located after the pollutant ID names and before the object properties start,
//			  and can differ for each pollutant.  They're stored as 4-byte integers with the following codes:
//		          0: mg/L
//				  1: ug/L
//				  2: counts/L
//		      Probably the best way to do this would not be here -- instead write a function that takes
//	          NPolluts and ObjPropPos, jump to ObjPropPos, count backward (NPolluts * 4), then read forward
//			  to get the units for each pollutant
//
{
	int errorcode = 0;

	*unitFlag = -1;
    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
	else
	{
		switch (code)
		{
		case flow_rate:			*unitFlag = smoapi->FlowUnits;
			break;
//		case concentration:		*unitFlag = ConcUnits;
//			break;
		default:                errorcode = 421;
		}
	}

	return errorcode;
}


int DLLEXPORT SMO_getPollutantUnits(SMOutputAPI* smoapi, int pollutantIndex, int* unitFlag)
//
//   Purpose: Return integer flag representing the units that the given pollutant is measured in.
//	          Concentration units are located after the pollutant ID names and before the object properties start,
//			  and are stored for each pollutant.  They're stored as 4-byte integers with the following codes:
//		          0: mg/L
//				  1: ug/L
//				  2: count/L
//
//   pollutantIndex: valid values are 0 to Npolluts-1
{
	int errorcode = 0;
    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
	else if (pollutantIndex < 0 || pollutantIndex >= smoapi->Npolluts) errorcode = 423;
    else
    {
        int offset = smoapi->ObjPropPos - (smoapi->Npolluts - pollutantIndex) * RECORDSIZE;
        fseek(smoapi->file, offset, SEEK_SET);
        fread(unitFlag, RECORDSIZE, 1, smoapi->file);
    }

	return errorcode;
}

int DLLEXPORT SMO_getStartTime(SMOutputAPI* smoapi, double* time)
//
//	Purpose: Returns start date.
//
{
	int errorcode = 0;

	*time = -1;
    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
	else
		*time = smoapi->StartDate;

	return errorcode;
}


int DLLEXPORT SMO_getTimes(SMOutputAPI* smoapi, SMO_time code, int* time)
//
//   Purpose: Returns step size and number of periods.
//
{
	int errorcode = 0;

	*time = -1;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
	else
	{
		switch (code)
		{
		case reportStep:  *time = smoapi->ReportStep;
			break;
		case numPeriods:  *time = smoapi->Nperiods;
			break;
		default:           errorcode = 421;
		}
	}

	return errorcode;
}

int DLLEXPORT SMO_getElementName(SMOutputAPI* smoapi, SMO_elementType type,
		int index, char* name, int length)
//
//  Purpose: Given an element index returns the element name.
//
//  Note: The caller is responsible for allocating memory for the char array
//    name. The caller passes the length of the array allocated. The name may
//    be truncated if an array of adequate length is not passed.
//
{
	int idx, errorcode = 0;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
	else
	{
		// Initialize the name array if necessary
		if (smoapi->elementNames == NULL) initElementNames(smoapi);

		switch (type)
		{
		case subcatch:
			if (index < 0 || index >= smoapi->Nsubcatch)
				errorcode = 423;
			else
				idx = index;
			break;

		case node:
			if (index < 0 || index >= smoapi->Nnodes)
				errorcode = 423;
			else
				idx = smoapi->Nsubcatch + index;
			break;

		case link:
			if (index < 0 || index >= smoapi->Nlinks)
				errorcode = 423;
			else
				idx = smoapi->Nsubcatch + smoapi->Nnodes + index;
			break;

		case sys:
			if (index < 0 || index >= smoapi->Npolluts)
				errorcode = 423;
			else
				idx = smoapi->Nsubcatch + smoapi->Nnodes + smoapi->Nlinks + index;
			break;

		default:
			errorcode = 421;
		}

		if (!errorcode)
			strncpy(name, smoapi->elementNames[idx].IDname, length);
	}

	return errorcode;
}


float* DLLEXPORT SMO_newOutValueSeries(SMOutputAPI* smoapi, long startPeriod,
	long seriesLength, long* length, int* errcode)
//
//  Purpose: Allocates memory for outValue Series.
//
//  Warning: Caller must free memory allocated by this function using SMO_free().
//
//  Valid values for startPeriod are 0 to Nperiods-1.
//  Valid values for seriesLength are 1 to Nperiods - startPeriod.
//  If seriesLength is specified as -1, it will be interpreted as Nperiods - startPeriod
//     so all values at and after startPeriod will fit.
{
	long size;
	float* array;

	if (seriesLength == -1)
	    seriesLength = smoapi->Nperiods - startPeriod;

    if (smoapi == NULL) *errcode = 410;
    else if (smoapi->file == NULL) *errcode = 411;
	else if (startPeriod < 0 || seriesLength < 1 || startPeriod + seriesLength > smoapi->Nperiods)
	    *errcode = 422;
	else
	{
		size = seriesLength;
		if (size > smoapi->Nperiods)
			size = smoapi->Nperiods;

		array = (float*)calloc(size, sizeof(float));
		*errcode = (MEMCHECK(array));

		*length = size;
		return array;
	}

	return NULL;
}


float* DLLEXPORT SMO_newOutValueArray(SMOutputAPI* smoapi, SMO_apiFunction func,
	SMO_elementType type, long* length, int* errcode)
//
// Purpose: Allocates memory for outValue Array.
//
//  Warning: Caller must free memory allocated by this function using SMO_free().
//
{
	long size;
	float* array;

    if (smoapi == NULL) *errcode = 410;
    else if (smoapi->file == NULL) *errcode = 411;
	else
	{
		switch (func)
		{
		case getAttribute:
			if (type == subcatch)
				size = smoapi->Nsubcatch;
			else if (type == node)
				size = smoapi->Nnodes;
			else if (type == link)
				size = smoapi->Nlinks;
			else // system
				size = 1;
		break;

		case getResult:
			if (type == subcatch)
				size = smoapi->SubcatchVars;
			else if (type == node)
				size = smoapi->NodeVars;
			else if (type == link)
				size = smoapi->LinkVars;
			else // system
				size = smoapi->SysVars;
		break;

		default: *errcode = 421;
			return NULL;
		}

		// Allocate memory for outValues
		array = (float*)calloc(size, sizeof(float));
		*errcode = (MEMCHECK(array));

		*length = size;
		return array;
	}

	return NULL;
}


int DLLEXPORT SMO_getSubcatchSeries(SMOutputAPI* smoapi, int subcatchIndex,
	SMO_subcatchAttribute attr, long startPeriod, long length, float* outValueSeries)
//
//  Purpose: Get time series results for particular attribute. Specify series
//  start and length using startPeriod and length respectively.
//
{
	int errorcode = 0;

	long k;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
	else if (subcatchIndex < 0 || subcatchIndex > smoapi->Nsubcatch) errorcode = 420;
    else if (startPeriod < 0 || startPeriod >= smoapi->Nperiods ||
        		startPeriod + length > smoapi->Nperiods) errorcode = 422;
	else if (outValueSeries == NULL) errorcode = 424;
	else
	{
		// loop over and build time series
		for (k = 0; k < length; k++)
			outValueSeries[k] = getSubcatchValue(smoapi, startPeriod + k,
			subcatchIndex, attr);
	}

	return errorcode;
}


int DLLEXPORT SMO_getNodeSeries(SMOutputAPI* smoapi, int nodeIndex, SMO_nodeAttribute attr,
	long startPeriod, long length, float* outValueSeries)
//
//  Purpose: Get time series results for particular attribute. Specify series
//  start and length using startPeriod and length respectively.
//
{
	int errorcode = 0;

	long k;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
	else if (nodeIndex < 0 || nodeIndex > smoapi->Nnodes) errorcode = 420;
    else if (startPeriod < 0 || length < 1 || startPeriod + length > smoapi->Nperiods) errorcode = 422;
	else if (outValueSeries == NULL) errorcode = 424;
	else
	{
		// loop over and build time series
		for (k = 0; k < length; k++)
			outValueSeries[k] = getNodeValue(smoapi, startPeriod + k,
			nodeIndex, attr);
	}

	return errorcode;
}


int DLLEXPORT SMO_getLinkSeries(SMOutputAPI* smoapi, int linkIndex, SMO_linkAttribute attr,
	long startPeriod, long length, float* outValueSeries)
//
//  Purpose: Get time series results for particular attribute. Specify series
//  start and length using startPeriod and length respectively.
//
{
	int errorcode = 0;

	long k;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
	else if (linkIndex < 0 || linkIndex > smoapi->Nlinks) errorcode = 420;
    else if (startPeriod < 0 || length < 1 || startPeriod + length > smoapi->Nperiods) errorcode = 422;
	else if (outValueSeries == NULL) errorcode = 424;
	else
	{
		// loop over and build time series
		for (k = 0; k < length; k++)
			outValueSeries[k] = getLinkValue(smoapi, startPeriod + k, linkIndex, attr);
	}

	return errorcode;
}



int DLLEXPORT SMO_getSystemSeries(SMOutputAPI* smoapi, SMO_systemAttribute attr,
	long startPeriod, long length, float *outValueSeries)
//
//  Purpose: Get time series results for particular attribute. Specify series
//  start and length using startPeriod and length respectively.
//
{
	int errorcode = 0;

	long k;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
    else if (startPeriod < 0 || length < 1 || startPeriod + length > smoapi->Nperiods) errorcode = 422;
	else if (outValueSeries == NULL) errorcode = 424;
	else
	{
		// loop over and build time series
		for (k = 0; k < length; k++)
			outValueSeries[k] = getSystemValue(smoapi, startPeriod + k, attr);
	}

	return errorcode;
}

int DLLEXPORT SMO_getSubcatchAttribute(SMOutputAPI* smoapi, long periodIndex,
	SMO_subcatchAttribute attr, float* outValueArray)
//
//   Purpose: For all subcatchments at given time, get a particular attribute.
//
{
	int errorcode = 0;

	long k;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
    else if (periodIndex < 0 || periodIndex >= smoapi->Nperiods) errorcode = 422;
	else if (outValueArray == NULL) errorcode = 424;
	else
	{
		// loop over and pull result
		for (k = 0; k < smoapi->Nsubcatch; k++)
			outValueArray[k] = getSubcatchValue(smoapi, periodIndex, k, attr);
	}

	return errorcode;
}



int DLLEXPORT SMO_getNodeAttribute(SMOutputAPI* smoapi, long periodIndex,
	SMO_nodeAttribute attr, float* outValueArray)
//
//  Purpose: For all nodes at given time, get a particular attribute.
//
{
	int errorcode = 0;

	long k;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
    else if (periodIndex < 0 || periodIndex >= smoapi->Nperiods) errorcode = 422;
	else if (outValueArray == NULL) errorcode = 424;
	else
	{
		// loop over and pull result
		for (k = 0; k < smoapi->Nnodes; k++)
			outValueArray[k] = getNodeValue(smoapi, periodIndex, k, attr);
	}

	return errorcode;
}

int DLLEXPORT SMO_getLinkAttribute(SMOutputAPI* smoapi, long periodIndex,
	SMO_linkAttribute attr, float* outValueArray)
//
//  Purpose: For all links at given time, get a particular attribute.
//
{
	int errorcode = 0;

	long k;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
    else if (periodIndex < 0 || periodIndex >= smoapi->Nperiods) errorcode = 422;
	else if (outValueArray == NULL) errorcode = 424;
	else
	{
		// loop over and pull result
		for (k = 0; k < smoapi->Nlinks; k++)
			outValueArray[k] = getLinkValue(smoapi, periodIndex, k, attr);
	}

	return errorcode;
}


int DLLEXPORT SMO_getSystemAttribute(SMOutputAPI* smoapi, long periodIndex,
	SMO_systemAttribute attr, float* outValueArray)
//
//  Purpose: For the system at given time, get a particular attribute.
//
{
	int errorcode = 0;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
    else if (periodIndex < 0 || periodIndex >= smoapi->Nperiods) errorcode = 422;
	else if (outValueArray == NULL) errorcode = 424;
	else
	{
		// don't need to loop since there's only one system
		outValueArray[0] = getSystemValue(smoapi, periodIndex, attr);
	}

	return errorcode;
}

int DLLEXPORT SMO_getSubcatchResult(SMOutputAPI* smoapi, long periodIndex, int subcatchIndex,
	float* outValueArray)
//
// Purpose: For a subcatchment at given time, get all attributes.
//
{
	int errorcode = 0;

	F_OFF offset;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
    else if (periodIndex < 0 || periodIndex >= smoapi->Nperiods) errorcode = 422;
    else if (subcatchIndex < 0 || subcatchIndex > smoapi->Nsubcatch) errorcode = 423;
    else if (outValueArray == NULL) errorcode = 424;
	else
	{
		// --- compute offset into output file
		offset = smoapi->ResultsPos + (periodIndex)*smoapi->BytesPerPeriod + 2 * RECORDSIZE;
		// add offset for subcatchment
		offset += (subcatchIndex*smoapi->SubcatchVars)*RECORDSIZE;

		fseeko64(smoapi->file, offset, SEEK_SET);
		fread(outValueArray, RECORDSIZE, smoapi->SubcatchVars, smoapi->file);
	}

	return errorcode;
}


int DLLEXPORT SMO_getNodeResult(SMOutputAPI* smoapi, long periodIndex, int nodeIndex,
	float* outValueArray)
//
//	Purpose: For a node at given time, get all attributes.
//
{
	int errorcode = 0;

	F_OFF offset;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
    else if (periodIndex < 0 || periodIndex >= smoapi->Nperiods) errorcode = 422;
    else if (nodeIndex < 0 || nodeIndex > smoapi->Nnodes) errorcode = 423;
    else if (outValueArray == NULL) errorcode = 424;
	else
	{
		// calculate byte offset to start time for series
		offset = smoapi->ResultsPos + (periodIndex)*smoapi->BytesPerPeriod + 2 * RECORDSIZE;
		// add offset for subcatchment and node
		offset += (smoapi->Nsubcatch*smoapi->SubcatchVars + nodeIndex*smoapi->NodeVars)*RECORDSIZE;

		fseeko64(smoapi->file, offset, SEEK_SET);
		fread(outValueArray, RECORDSIZE, smoapi->NodeVars, smoapi->file);
	}

	return errorcode;
}


int DLLEXPORT SMO_getLinkResult(SMOutputAPI* smoapi, long periodIndex, int linkIndex,
	float* outValueArray)
//
//	Purpose: For a link at given time, get all attributes.
//
{
	int errorcode = 0;

	F_OFF offset;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
    else if (periodIndex < 0 || periodIndex >= smoapi->Nperiods) errorcode = 422;
    else if (linkIndex < 0 || linkIndex > smoapi->Nlinks) errorcode = 423;
    else if (outValueArray == NULL) errorcode = 424;
	else
	{
		// calculate byte offset to start time for series
		offset = smoapi->ResultsPos + (periodIndex)*smoapi->BytesPerPeriod + 2 * RECORDSIZE;
		// add offset for subcatchment and node and link
		offset += (smoapi->Nsubcatch*smoapi->SubcatchVars
			+ smoapi->Nnodes*smoapi->NodeVars + linkIndex*smoapi->LinkVars)*RECORDSIZE;

		fseeko64(smoapi->file, offset, SEEK_SET);
		fread(outValueArray, RECORDSIZE, smoapi->LinkVars, smoapi->file);
	}

	return errorcode;
}

int DLLEXPORT SMO_getSystemResult(SMOutputAPI* smoapi, long periodIndex, int dummyIndex,
		float* outValueArray)
//
//	Purpose: For the system at given time, get all attributes.
//
{
	int errorcode = 0;

	F_OFF offset;

    if (smoapi == NULL) errorcode = 410;
    else if (smoapi->file == NULL) errorcode = 411;
    else if (periodIndex < 0 || periodIndex >= smoapi->Nperiods) errorcode = 422;
    else if (outValueArray == NULL) errorcode = 424;
	{
		// calculate byte offset to start time for series
		offset = smoapi->ResultsPos + (periodIndex)*smoapi->BytesPerPeriod + 2 * RECORDSIZE;
		// add offset for subcatchment and node and link (system starts after the last link)
		offset += (smoapi->Nsubcatch*smoapi->SubcatchVars + smoapi->Nnodes*smoapi->NodeVars
			+ smoapi->Nlinks*smoapi->LinkVars)*RECORDSIZE;

		fseeko64(smoapi->file, offset, SEEK_SET);
		fread(outValueArray, RECORDSIZE, smoapi->SysVars, smoapi->file);
	}

	return errorcode;
}

void DLLEXPORT SMO_free(float *array)
//
//  Purpose: frees memory allocated using SMO_newOutValueSeries() or
//  SMO_newOutValueArray().
//
{
	if (array != NULL)
		free(array);
}


int DLLEXPORT SMO_close(SMOutputAPI* smoapi)
//
//   Purpose: Clean up after and close Output API
//
{
	int i, n, errorcode = 0;

	if (smoapi == NULL) errorcode = 410;
	else if (smoapi->file == NULL) errorcode = 411;
	else
	{
		if (smoapi->elementNames != NULL)
		{
			n = smoapi->Nsubcatch + smoapi->Nnodes + smoapi->Nlinks + smoapi->Npolluts;

			for(i = 0; i < n; i++)
				free(smoapi->elementNames[i].IDname);
		}

		fclose(smoapi->file);
		free(smoapi);
		smoapi = NULL;
	}

	return errorcode;
}

void DLLEXPORT SMO_errMessage(int errcode, char* errmsg, int n)
//
//  Purpose: takes error code returns error message
//
// ERR410 "Error 410: SMO_init has not been called"
// ERR411 "Error 411: SMO_open has not been called"
// ERR414 "Error 414: memory allocation failure"
//
// ERR421 "Input Error 421: invalid parameter code"
// ERR422 "Input Error 422: reporting period index out of range"
// ERR423 "Input Error 423: element index out of range"
// ERR424 "Input Error 424: no memory allocated for results"
//
// ERR434 "File Error  434: unable to open binary output file"
// ERR435 "File Error  435: invalid file - not created by SWMM"
// ERR436 "File Error  436: invalid file - contains no results"
// ERR437 "File Error  437: invalid file - model run issued warnings"
//
// ERR440 "ERROR 440: an unspecified error has occurred"
{
	switch (errcode)
	{
	case 410:
		strncpy(errmsg, ERR410, n);
		break;
	case 411:
		strncpy(errmsg, ERR411, n);
		break;
	case 414:
		strncpy(errmsg, ERR414, n);
		break;
	case 421:
		strncpy(errmsg, ERR421, n);
		break;
	case 422:
		strncpy(errmsg, ERR422, n);
		break;
	case 423:
		strncpy(errmsg, ERR423, n);
		break;
	case 424:
		strncpy(errmsg, ERR424, n);
		break;
	case 434:
		strncpy(errmsg, ERR434, n);
		break;
	case 435:
		strncpy(errmsg, ERR435, n);
		break;
	case 436:
		strncpy(errmsg, ERR436, n);
		break;
	case 437:
		strncpy(errmsg, ERR437, n);
		break;
	default:
		strncpy(errmsg, ERR440, n);
	}
}


// Local functions:
int validateFile(SMOutputAPI* smoapi)
{
	INT4 magic1, magic2, errcode;
	int errorcode = 0;

	// --- fast forward to end and read epilogue
	fseeko64(smoapi->file, -6 * RECORDSIZE, SEEK_END);
	fread(&(smoapi->IDPos), RECORDSIZE, 1, smoapi->file);
	fread(&(smoapi->ObjPropPos), RECORDSIZE, 1, smoapi->file);
	fread(&(smoapi->ResultsPos), RECORDSIZE, 1, smoapi->file);
	fread(&(smoapi->Nperiods), RECORDSIZE, 1, smoapi->file);
	fread(&errcode, RECORDSIZE, 1, smoapi->file);
	fread(&magic2, RECORDSIZE, 1, smoapi->file);

	// --- rewind and read magic number from beginning of the file
	fseeko(smoapi->file, 0L, SEEK_SET);
	fread(&magic1, RECORDSIZE, 1, smoapi->file);

	// Is this a valid SWMM binary output file?
	if (magic1 != magic2) errorcode = 435;
	// Does the binary file contain results?
	else if (smoapi->Nperiods <= 0) errorcode = 436;
	// Were there problems with the model run?
	else if (errcode != 0) errorcode = 437;

	return errorcode;
}

void initElementNames(SMOutputAPI* smoapi)
{
	int j, numNames;

	numNames = smoapi->Nsubcatch + smoapi->Nnodes + smoapi->Nlinks + smoapi->Npolluts;

	// allocate memory for array of idEntries
	smoapi->elementNames = (idEntry*)calloc(numNames, sizeof(idEntry));

	// Position the file to the start of the ID entries
	fseeko64(smoapi->file, smoapi->IDPos, SEEK_SET);

	for(j=0;j<numNames;j++)
	{
		fread(&(smoapi->elementNames[j].length), RECORDSIZE, 1, smoapi->file);
		smoapi->elementNames[j].IDname = calloc(smoapi->elementNames[j].length + 1, sizeof(char));
		fread(smoapi->elementNames[j].IDname, sizeof(char), smoapi->elementNames[j].length, smoapi->file);
	}
}

double getTimeValue(SMOutputAPI* smoapi, long timeIndex)
{
	F_OFF offset;
	double value;

	// --- compute offset into output file
	offset = smoapi->ResultsPos + timeIndex*smoapi->BytesPerPeriod;

	// --- re-position the file and read the result
	fseeko64(smoapi->file, offset, SEEK_SET);
	fread(&value, RECORDSIZE * 2, 1, smoapi->file);

	return value;
}

float getSubcatchValue(SMOutputAPI* smoapi, long timeIndex, int subcatchIndex,
		SMO_subcatchAttribute attr)
{
	F_OFF offset;
	float value;

	// --- compute offset into output file
	offset = smoapi->ResultsPos + timeIndex*smoapi->BytesPerPeriod + 2*RECORDSIZE;
	// offset for subcatch
	offset += RECORDSIZE*(subcatchIndex*smoapi->SubcatchVars + attr);

	// --- re-position the file and read the result
	fseeko64(smoapi->file, offset, SEEK_SET);
	fread(&value, RECORDSIZE, 1, smoapi->file);

	return value;
}

float getNodeValue(SMOutputAPI* smoapi, long timeIndex, int nodeIndex,
		SMO_nodeAttribute attr)
{
	F_OFF offset;
	float value;

	// --- compute offset into output file
	offset = smoapi->ResultsPos + timeIndex*smoapi->BytesPerPeriod + 2*RECORDSIZE;
	// offset for node
	offset += RECORDSIZE*(smoapi->Nsubcatch*smoapi->SubcatchVars + nodeIndex*smoapi->NodeVars + attr);

	// --- re-position the file and read the result
	fseeko64(smoapi->file, offset, SEEK_SET);
	fread(&value, RECORDSIZE, 1, smoapi->file);

	return value;
}


float getLinkValue(SMOutputAPI* smoapi, long timeIndex, int linkIndex,
		SMO_linkAttribute attr)
{
	F_OFF offset;
	float value;

	// --- compute offset into output file
	offset = smoapi->ResultsPos + timeIndex*smoapi->BytesPerPeriod + 2*RECORDSIZE;
	// offset for link
	offset += RECORDSIZE*(smoapi->Nsubcatch*smoapi->SubcatchVars + smoapi->Nnodes*smoapi->NodeVars +
		linkIndex*smoapi->LinkVars + attr);

	// --- re-position the file and read the result
	fseeko64(smoapi->file, offset, SEEK_SET);
	fread(&value, RECORDSIZE, 1, smoapi->file);

	return value;
}

float getSystemValue(SMOutputAPI* smoapi, long timeIndex,
		SMO_systemAttribute attr)
{
	F_OFF offset;
	float value;

	// --- compute offset into output file
	offset = smoapi->ResultsPos + timeIndex*smoapi->BytesPerPeriod + 2*RECORDSIZE;
	//  offset for system
	offset += RECORDSIZE*(smoapi->Nsubcatch*smoapi->SubcatchVars + smoapi->Nnodes*smoapi->NodeVars +
		smoapi->Nlinks*smoapi->LinkVars + attr);

	// --- re-position the file and read the result
	fseeko64(smoapi->file, offset, SEEK_SET);
	fread(&value, RECORDSIZE, 1, smoapi->file);

	return value;
}
