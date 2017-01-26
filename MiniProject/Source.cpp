#include <cv.h>         
#include <highgui.h>  
#include <cxcore.h>  

int CVCONTOUR_APPROX_LEVEL = 2;   // Approx.threshold - the bigger, the simpler the boundary  
int CVCLOSE_ITR = 1;              //Iterations of erosion and dilation 
#define WHITE CV_RGB(255,255,255)  
#define BLACK CV_RGB(0,0,0)  
#define RED CV_RGB(0,255,0)
#define CHL 3
int num = 5; //Arbitrarily
CvRect bbs[20];
CvPoint centers[20];
int Counter = 0, Global = 0;
typedef struct ce {
	uchar   learnHigh[CHL];    // High side threshold for learning  
	uchar   learnLow[CHL];     // Low side threshold for learning  
	uchar   max[CHL];          // High side of box boundary  
	uchar   min[CHL];          // Low side of box boundary  
	int     t_last_update;     // To kill stale entries   
	int     stale;             // max negative run 
}code_element;

typedef struct code_book {
	code_element    **cb;
	int             numEntries;
	int             t;
}codeBook;

int updateCB(uchar *p, codeBook &c, unsigned *cbBounds, int numCHL){
	if (c.numEntries == 0) c.t = 0;
	c.t += 1;
	int n;
	unsigned int high[3], low[3];

	//Learning bounds
	for (n = 0; n<numCHL; n++){
		high[n] = *(p + n) + *(cbBounds + n);
		if (high[n] > 255) high[n] = 255;
		low[n] = *(p + n) - *(cbBounds + n);
		if (low[n] < 0) low[n] = 0;
	}

	int matchChannel;
	int i;
	for (i = 0; i<c.numEntries; i++){
		matchChannel = 0;
		for (n = 0; n<numCHL; n++){//numCHL = 3
			//Found an entry for this channel 
			if ((c.cb[i]->learnLow[n] <= *(p + n)) && (*(p + n) <= c.cb[i]->learnHigh[n]))  matchChannel++;
		}

		if (matchChannel == numCHL){
			c.cb[i]->t_last_update = c.t;
			for (n = 0; n<numCHL; n++){
				if (c.cb[i]->max[n] < *(p + n))   c.cb[i]->max[n] = *(p + n);
				else if (c.cb[i]->min[n] > *(p + n))   c.cb[i]->min[n] = *(p + n);
			}
			break;
		}
	}

	//Have to add new entry
	if (i == c.numEntries){
		code_element **temp = new code_element*[c.numEntries + 1];
		for (int j = 0; j < c.numEntries; j++) temp[j] = c.cb[j];
		temp[c.numEntries] = new code_element;
		if (c.numEntries) delete[] c.cb;
		c.cb = temp;
		for (n = 0; n<numCHL; n++){
			c.cb[c.numEntries]->learnHigh[n] = high[n]; //Update local learning bound to the learnHigh in codebook element
			c.cb[c.numEntries]->learnLow[n] = low[n]; //Update local learning bound to the learnLow in codebook element
			c.cb[c.numEntries]->max[n] = *(p + n); //Exact value of nth chanel to max
			c.cb[c.numEntries]->min[n] = *(p + n); //Exact value of ntgh channel to min
		}
		c.cb[c.numEntries]->t_last_update = c.t;
		c.cb[c.numEntries]->stale = 0;
		c.numEntries += 1;
	}

	for (int k = 0; k < c.numEntries; k++){
		int negRun = c.t - c.cb[k]->t_last_update;
		if (c.cb[k]->stale < negRun) c.cb[k]->stale = negRun;
	}

	//ADJUST LEARNING BOUNDS  
	for (n = 0; n < numCHL; n++){
		if (c.cb[i]->learnHigh[n] < high[n]) c.cb[i]->learnHigh[n]++;
		if (c.cb[i]->learnLow[n] > low[n]) c.cb[i]->learnLow[n]--;
	}
	return(i);
}


uchar bgSubtraction(uchar *p, codeBook &c, int numCHL, int *minMod, int *maxMod){
	int matchChl, i;
	for (i = 0; i<c.numEntries; i++){
		matchChl = 0;
		for (int n = 0; n < numCHL; n++){ //numCHL = 3
			if ((c.cb[i]->min[n] - minMod[n] <= *(p + n)) && (*(p + n) <= c.cb[i]->max[n] + maxMod[n]))
				matchChl++;
			else break;
		}
		if (matchChl == numCHL) break;
	}
	if (i == c.numEntries) return(255);
	return(0);
}

int clearStale(codeBook &c){
	int staleThresh = c.t >> 2;
	int *keep = new int[c.numEntries];
	int keepCounter = 0;

	for (int i = 0; i<c.numEntries; i++){
		if (c.cb[i]->stale > staleThresh) keep[i] = 0;
		else{
			keep[i] = 1;
			keepCounter += 1;
		}
	}

	c.t = 0;
	code_element **temp = new code_element*[keepCounter];
	int k = 0;
	for (int i = 0; i<c.numEntries; i++){
		if (keep[i]){
			temp[k] = c.cb[i];
			temp[k]->stale = 0;
			temp[k]->t_last_update = 0;
			k++;
		}
	}
	//Clean memory
	delete[] c.cb;
	delete[] keep;
	c.cb = temp;
	int clear = c.numEntries - keepCounter;
	c.numEntries = keepCounter;
	return(clear);
}

void drawBox(IplImage *mask, IplImage *fmask, float perimScale, CvRect *bbs, CvPoint *centers){
	static CvMemStorage* mem_store = NULL;
	static CvSeq* contours = NULL;
	cvMorphologyEx(mask, mask, NULL, NULL, CV_MOP_OPEN, CVCLOSE_ITR);
	cvMorphologyEx(mask, mask, NULL, NULL, CV_MOP_CLOSE, CVCLOSE_ITR );

	if (mem_store == NULL) mem_store = cvCreateMemStorage(0);
	else cvClearMemStorage(mem_store);

	CvContourScanner scanner = cvStartFindContours(mask, mem_store, sizeof(CvContour), CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);

	CvSeq* c;
	int numCont = 0;
	while ((c = cvFindNextContour(scanner)) != NULL){
		double length = cvContourPerimeter(c);
		double q = (mask->height + mask->width) / perimScale;
		if (length < q) cvSubstituteContour(scanner, NULL);
		else{
			CvSeq* c_new;
			c_new = cvApproxPoly(c, sizeof(CvContour), mem_store, CV_POLY_APPROX_DP, CVCONTOUR_APPROX_LEVEL, 1);
			cvSubstituteContour(scanner, c_new);
			numCont++;
		}
	}
	contours = cvEndFindContours(&scanner);
	cvZero(mask);
	IplImage *maskTemp;
	int i = 0;
	CvMoments moments;
	double M00, M01, M10;
	maskTemp = cvCloneImage(mask);

	for (i = 0, c = contours; c != NULL; c = c->h_next, i++){
		cvDrawContours(maskTemp, c, WHITE, WHITE, -1, CV_FILLED, 8);
		cvMoments(maskTemp, &moments, 1);
		M00 = cvGetSpatialMoment(&moments, 0, 0);
		M10 = cvGetSpatialMoment(&moments, 1, 0);
		M01 = cvGetSpatialMoment(&moments, 0, 1);
		centers[i].x = (int)(M10 / M00);
		centers[i].y = (int)(M01 / M00);
		bbs[i] = cvBoundingRect(c);
		int distanceX = abs(centers[i].x - bbs[i].x);
		int distanceY = abs(centers[i].y - bbs[i].y);
		cvRectangle(fmask, cvPoint(centers[i].x - distanceX, centers[i].y - distanceY), cvPoint(centers[i].x + distanceX, centers[i].y + distanceY), RED, 4, 8);
		if(Global >= 150) Counter++;
		cvZero(maskTemp);
		cvDrawContours(mask, c, WHITE, WHITE, -1, CV_FILLED, 8);
	}
	cvReleaseImage(&maskTemp);
}

int main()
{
	CvCapture*  capture = NULL;
	IplImage*   yuvImage = NULL;
	IplImage*   rawImage = NULL;
	IplImage*   ImaskCodeBook = NULL;
	IplImage*   ImaskCodeBookCC = NULL;
	codeBook*   cB = NULL;
	unsigned    cbBounds[CHL];
	uchar*      pColor = NULL; //YUV pointer  
	int         imageSize = 0, nCHL = CHL, minMod[CHL], maxMod[CHL];

	capture = cvCreateFileCapture("C:\\Users\\Mervyn\\Desktop\\Road.mp4");
	cvNamedWindow("Raw");
	cvNamedWindow("CodeBook");
	//cvNamedWindow("Filtered");

	if (!capture){
		printf("Fail to open the file\n");
		return -1;
	}
	
	rawImage = cvQueryFrame(capture);
	yuvImage = cvCreateImage(cvGetSize(rawImage), 8, 3); //8 bit depth and 3 CHL
	ImaskCodeBook = cvCreateImage(cvGetSize(rawImage), IPL_DEPTH_8U, 1);
	ImaskCodeBookCC = cvCreateImage(cvGetSize(rawImage), IPL_DEPTH_8U, 1);

	cvSet(ImaskCodeBook, cvScalar(255));
	imageSize = cvGetSize(rawImage).height * cvGetSize(rawImage).width;
	cB = new codeBook[imageSize];

	for (int i = 0; i<imageSize; i++) cB[i].numEntries = 0;

	for (int i = 0; i<nCHL; i++){
		cbBounds[i] = 7;
		minMod[i] = 10;
		maxMod[i] = 10;
	}

	for (int i = 0;; i++){
		Global = i;
		int learningFrame = 50;
		cvCvtColor(rawImage, yuvImage, CV_BGR2YCrCb); //Convert to YUV in raw to yuvImage
		if (i <= learningFrame){
			printf("Training count: %d\n", i);
			pColor = (uchar *)(yuvImage->imageData);
			for (int c = 0; c<imageSize; c++){
				updateCB(pColor, cB[c], cbBounds, nCHL);
				pColor += 3;
			}
			if (i == learningFrame) for (int c = 0; c<imageSize; c++) clearStale(cB[c]);
		}
		else{
			uchar maskPixelCodeBook;
			pColor = (uchar *)((yuvImage)->imageData); //3 channel yuv image  
			uchar *pMask = (uchar *)((ImaskCodeBook)->imageData); //1 channel image  
			for (int c = 0; c<imageSize; c++){
				maskPixelCodeBook = bgSubtraction(pColor, cB[c], nCHL, minMod, maxMod);
				if (i % 100 == 0) {
					updateCB(pColor, cB[c], cbBounds, nCHL);
				}
				*pMask++ = maskPixelCodeBook;
				pColor += 3;
			}
			
			cvCopy(ImaskCodeBook, ImaskCodeBookCC);
			drawBox(ImaskCodeBookCC, yuvImage, 4.0, bbs, centers);
		
		}
		cvCvtColor(yuvImage, yuvImage, CV_YCrCb2BGR); 
		if (!(rawImage = cvQueryFrame(capture))) break;
		cvShowImage("Raw", yuvImage);
		cvShowImage("CodeBook", ImaskCodeBook);
		cvShowImage("Filtered", ImaskCodeBookCC);
		
		if (cvWaitKey(30) == 27) break;
	}
	printf("%d\n", Counter);
	cvReleaseCapture(&capture);
	if (yuvImage) cvReleaseImage(&yuvImage);
	if (ImaskCodeBook) cvReleaseImage(&ImaskCodeBook);
	cvDestroyAllWindows();
	delete[] cB;
	return 0;
}