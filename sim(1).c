//gcc 5.4.0
//bimodal predictor
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>

#define MIN_ARGS_REQUIRED   4
#define BIMODAL_ARGS_REQUIRED   4
#define GSHARE_ARGS_REQUIRED    5
#define HYBRID_ARGS_REQUIRED    6
#define YES 1
#define NO 0
#define TAKEN 1
#define NOT_TAKEN 0

#ifdef DEBUG
# define DEBUG_PRINT(x) printf x
#else
# define DEBUG_PRINT(x) do {} while (0)
#endif

uint8_t *bimodal_prediction_table;
uint8_t *gshare_prediction_table;
uint8_t *chooser_table;
unsigned int bimodalPredictorSize = 0;
unsigned int gsharePredictorSize = 0;


int n = 0; //Number of bits in global history register
int k = 0; //Number of bits used to index in the chooser table

int predictorType = 0;
int selectedPredictor;
uint16_t historyregister;
unsigned int numberOfCorrectPredictions = 0;
unsigned int numberOfMispredictions = 0;
unsigned int numberOfBranches = 0;
const char *filePath = NULL;
FILE *fp = 0;

void openfile()
{
    fp = fopen(filePath, "r");
    if (fp) {
        DEBUG_PRINT(("File opened: %s, %p\n", filePath, fp));
    }
}

/*
 * Bi Modal Branch Predictor
 */
uint8_t bmpredictor(unsigned int branchAddress)
{
    // Read the bimodal prediction table, if the value is >= 4, predict as taken
    unsigned int branchAddressIdx = ((branchAddress >> 2) & ((1 << bimodalPredictorSize) - 1));
    uint8_t bmprediction;
    bmprediction = (uint8_t)bimodal_prediction_table[branchAddressIdx];
    if (bmprediction >= 4) {
        return TAKEN;
    } else {
        return NOT_TAKEN;
    }
}

void updatebmpredictor(uint8_t actualOutcome, unsigned int branchAddress)
{
    unsigned int branchAddressIdx = ((branchAddress >> 2) & ((1 << bimodalPredictorSize) - 1));
    uint8_t bmprediction;
    bmprediction = (uint8_t)bimodal_prediction_table[branchAddressIdx];
    
    if (actualOutcome == TAKEN) {
        if (bmprediction < 7) {
            bimodal_prediction_table[branchAddressIdx]++;
        }
    } else {
        if (bmprediction > 0) {
            bimodal_prediction_table[branchAddressIdx]--;
        }
    }
}

/*
 * Gshare Branch Predictor
 */
uint8_t gsharepredictor(unsigned int branchAddress)
{
    // Get lower order m bits from the address and n bits from branch history register
    uint16_t branchAddressLowerMbits = ((branchAddress >> 2) & ((1 << gsharePredictorSize) - 1));
    uint16_t branchAddressLowerNbits = ((branchAddress >> 2) & ((1 << n) - 1));
    uint16_t branchHistoryLowerNbits = historyregister & ((1 << n) - 1);
    
    uint16_t branchAddressIdx = (branchAddressLowerNbits ^ branchHistoryLowerNbits) | (branchAddressLowerMbits & (~((1 << n) - 1)) );
    uint8_t gshareprediction;
    gshareprediction = (uint8_t)gshare_prediction_table[branchAddressIdx];
    if (gshareprediction >= 4) {
        return TAKEN;
    } else {
        return NOT_TAKEN;
    }
}

void updategsharepredictor(uint8_t actualOutcome, unsigned int branchAddress)
{
    uint16_t branchAddressLowerMbits = ((branchAddress >> 2) & ((1 << gsharePredictorSize) - 1));
    uint16_t branchAddressLowerNbits = ((branchAddress >> 2) & ((1 << n) - 1));
    uint16_t branchHistoryLowerNbits = historyregister & ((1 << n) - 1);
    
    uint16_t branchAddressIdx = (branchAddressLowerNbits ^ branchHistoryLowerNbits) | (branchAddressLowerMbits & (~((1 << n) - 1)) );
    uint8_t gshareprediction = (uint8_t)gshare_prediction_table[branchAddressIdx];
    
    if (actualOutcome == TAKEN) {
        if (gshareprediction < 7) {
            gshare_prediction_table[branchAddressIdx]++;
        }
    } else {
        if (gshareprediction > 0) {
            gshare_prediction_table[branchAddressIdx]--;
        }
    }
}

void updatehistoryregister(uint8_t actualOutcome)
{
    if (actualOutcome == TAKEN) {
        historyregister = (historyregister >> 1) | (1<<(n-1));
    } else {
        historyregister = (historyregister >> 1) | (0<<(n-1));
    }
}

/*
 * Hybrid Branch Predictor
 */
uint8_t hybridpredictor(unsigned int branchAddress)
{
    // Get the choose table index
    uint16_t chooserTableIdx = ((branchAddress >> 2) & ((1 << k) - 1));
    uint8_t gSharePrediction = gsharepredictor(branchAddress);
    uint8_t bimodalPrediction = bmpredictor(branchAddress);
    DEBUG_PRINT(("Idx %u\t", chooserTableIdx));

    if (chooser_table[chooserTableIdx] >= 2) {
        selectedPredictor = 1;
        return gSharePrediction;
    } else {
        selectedPredictor = 0;
        return bimodalPrediction;
    }
}

void updatehybridpredictor(uint8_t actualOutcome, unsigned int branchAddress)
{
    uint16_t chooserTableIdx = ((branchAddress >> 2) & ((1 << k) - 1));
    uint8_t gSharePrediction = gsharepredictor(branchAddress);
    uint8_t bimodalPrediction = bmpredictor(branchAddress);

    // Update the selected predictor and the history register
    switch (selectedPredictor) {
        case 0:
            updatebmpredictor(actualOutcome, branchAddress);
            break;
        case 1:
            updategsharepredictor(actualOutcome, branchAddress);
            break;
    }
    // Update the history register regardless of who we chose in last step
    updatehistoryregister(actualOutcome);
    
    // Update the chooser when both are different
    if (gSharePrediction != bimodalPrediction) {
        // If gshare was correct
        if (gSharePrediction == actualOutcome) {
            if (chooser_table[chooserTableIdx] < 3) {
                chooser_table[chooserTableIdx]++;
            }
        }
        // Bi modal was correct
        else {
            if (chooser_table[chooserTableIdx] > 0) {
                chooser_table[chooserTableIdx]--;
            }
        }
    }
}


void runBranchPredictor()
{
    char branchAddress[7];
    char branchOutcome[2];
    uint8_t prediction;
    uint8_t correctoutcome;

    if(fp)
    {
        while(!feof(fp))
        {
            fscanf(fp,"%6s ", branchAddress);
            unsigned int branchAddressInt = (int)strtol(branchAddress, NULL, 16);
            DEBUG_PRINT(("%s  ",branchAddress));
            fflush(stdout);

            fscanf(fp,"%3s", branchOutcome);
            if (feof(fp)) {
                break;
            }
            DEBUG_PRINT(("%s\t", branchOutcome));
            fflush(stdout);
            if (strlen(branchAddress) == 6 && strlen(branchOutcome) == 1) {
                switch (predictorType) {
                    case 0:
                        // call bi modal prediction
                        prediction = bmpredictor(branchAddressInt);
                        break;
                    case 1:
                        // call gshare predictor
                        prediction = gsharepredictor(branchAddressInt);
                        break;
                    case 2:
                        // call hybrid predictior
                        prediction = hybridpredictor(branchAddressInt);
                        break;
                }
                
                DEBUG_PRINT(("Prediction: %u\t", prediction));

                // Get the correct outcome and check if our prediction was right.
                correctoutcome = (branchOutcome[0] == 't') ? TAKEN : NOT_TAKEN;
                if (correctoutcome != prediction) {
                    DEBUG_PRINT(("Wrong\n"));
                    numberOfMispredictions++;
                } else {
                    DEBUG_PRINT(("Right\n"));
                }

                // Update the predictor information
                switch (predictorType) {
                    case 0:
                        updatebmpredictor(correctoutcome ,branchAddressInt);
                        break;
                    case 1:
                        updategsharepredictor(correctoutcome, branchAddressInt);
                        updatehistoryregister(correctoutcome);
                        break;
                    case 2:
                        updatehybridpredictor(correctoutcome, branchAddressInt);
                        break;
                }
                numberOfBranches++;
            }
        } // END_WHILE
        float rate = (float)numberOfMispredictions/(float)numberOfBranches;
        printf("number of predictions:      %d\nnumber of mispredictions:    %d\nmisprediction rate:          %f%%\n", (numberOfBranches) ,numberOfMispredictions, (rate*100));
    } else {
        printf("Error: File pointer no available");
    }
}

void print_tables(int bSize,int gSize,int cSize)
    {
      switch (predictorType) {
      case 0:
	printf("FINAL BIMODAL CONTENTS\n");
	for (int i=0; i<bSize;i++) {
	  printf("%d	%d\n ",i,bimodal_prediction_table[i]);
	}
	break;
      case 1:
	printf("FINAL GSHARE CONTENTS\n");
	for (int i =0; i<gSize; i++) {
	  printf("%d	%d\n", i, gshare_prediction_table[i]);

	}
	break;
      case 2:
	printf("FINAL CHOOSER CONTENTS\n");
	for(int i=0; i<cSize;i++) {
	  printf("%d	%d\n",i,chooser_table[i]);
	}
	printf("FINAL GSHARE CONTENTS\n");
	for(int i=0;i<gSize;i++) {
	  printf("%d	%d\n", i, gshare_prediction_table[i]);
	}
	printf("FINAL BIMODAL CONTENTS\n");
	for (int i=0;i<bSize;i++) {
            printf("%d	%d\n",i,bimodal_prediction_table[i]);
        }
        break;
	
	
      }
	
    }


int  main(int argc, const char * argv[])
{
	int chooserSize;
    int m1 = 0;
    int m2 = 0;
    if (argc < MIN_ARGS_REQUIRED) {
        printf("Minimum number of arguments are:%d\n", BIMODAL_ARGS_REQUIRED);
        return 0;
    }
    
    if (strcmp(argv[1], "bimodal") == 0) {
        if (argc < BIMODAL_ARGS_REQUIRED) {
            printf("Minimum number of arguments are:%d\n", BIMODAL_ARGS_REQUIRED);
            return 0;
        }
        predictorType = 0;
        m1 = atoi(argv[2]);
        bimodalPredictorSize = m1;
        filePath = argv[3];
        printf ("COMMAND\n./sim Bimodal %d %s\nOUTPUT\n",m1, filePath);
    }
    else if (strcmp(argv[1], "gshare") == 0) {
        if (argc < GSHARE_ARGS_REQUIRED) {
            printf("Minimum number of arguments are:%d\n", GSHARE_ARGS_REQUIRED);
            return 0;
        }
        predictorType = 1;
        m1 = atoi(argv[2]);
        gsharePredictorSize = m1;
        n = atoi(argv[3]);
        filePath = argv[4];
        printf ("COMMAND\n./sim Gshare %d %d %s\nOUTPUT\n", m1, n, filePath);
    }
    
    else if (strcmp(argv[1], "hybrid") == 0) {
        if (argc < HYBRID_ARGS_REQUIRED) {
            printf("Minimum number of arguments are:%d\n", HYBRID_ARGS_REQUIRED);
            return 0;
        }
        predictorType = 2;
        k = atoi(argv[2]);
        m1 = atoi(argv[3]);
        n = atoi(argv[4]);
        m2 = atoi(argv[5]);
        filePath = argv[6];
        bimodalPredictorSize = m2;
        gsharePredictorSize = m1;
        chooserSize = (1 << k);
        chooser_table = (uint8_t *)malloc(chooserSize * sizeof(uint8_t));
        for(int i=0; i<chooserSize; i++) {
            chooser_table[i] = 1;
        }
        printf ("COMMAND\n./sim Hybrid %d %d %d %d %s\nOUTPUT\n", k, m1, n, m2, filePath);
    }
    
    int bmsize = (1 << bimodalPredictorSize); //calculate 2^m
    bimodal_prediction_table = (uint8_t *)malloc(bmsize * sizeof(uint8_t));
    for(int i=0; i<bmsize; i++) {
        bimodal_prediction_table[i] = 4;
    }
    
    int gshareSize = (1 << gsharePredictorSize);
    gshare_prediction_table = (uint8_t *)malloc(gshareSize * sizeof(uint8_t));
    //initialize all entries with 4
    for(int i=0; i<gshareSize; i++) {
        gshare_prediction_table[i] = 4;
    }

    historyregister = 0;
    openfile();
    runBranchPredictor();
    // return 0;
    print_tables(bmsize,gshareSize,chooserSize);
    
      return 0;
}




