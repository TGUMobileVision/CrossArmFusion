#include "stereo_sdf/SGMStereo.h"
#include <stack>
#include <algorithm>
#include <nmmintrin.h>
#include <stdexcept>
#include<cmath>
// Default parameters
const int SGMSTEREO_DEFAULT_DISPARITY_TOTAL = 256;
const double SGMSTEREO_DEFAULT_DISPARITY_FACTOR = 256;
const int SGMSTEREO_DEFAULT_SOBEL_CAP_VALUE = 15;
const int SGMSTEREO_DEFAULT_CENSUS_WINDOW_RADIUS = 2;
const double SGMSTEREO_DEFAULT_CENSUS_WEIGHT_FACTOR = 1.0/6.0;
const int SGMSTEREO_DEFAULT_AGGREGATION_WINDOW_RADIUS = 1;

/* MIDDLEBURY P1 & P2 */
//const int SGMSTEREO_DEFAULT_SMOOTHNESS_PENALTY_SMALL = 100;
//const int SGMSTEREO_DEFAULT_SMOOTHNESS_PENALTY_LARGE = 1600;

/* KITTI P1 & P2 */
const int SGMSTEREO_DEFAULT_SMOOTHNESS_PENALTY_SMALL = 100;
const int SGMSTEREO_DEFAULT_SMOOTHNESS_PENALTY_LARGE = 1000;

const int SGMSTEREO_DEFAULT_CONSISTENCY_THRESHOLD = 1;

SGMStereo::SGMStereo() : disparityTotal_(SGMSTEREO_DEFAULT_DISPARITY_TOTAL),
             disparityFactor_(SGMSTEREO_DEFAULT_DISPARITY_FACTOR),
             sobelCapValue_(SGMSTEREO_DEFAULT_SOBEL_CAP_VALUE),
             censusWindowRadius_(SGMSTEREO_DEFAULT_CENSUS_WINDOW_RADIUS),
             censusWeightFactor_(SGMSTEREO_DEFAULT_CENSUS_WEIGHT_FACTOR),
             aggregationWindowRadius_(SGMSTEREO_DEFAULT_AGGREGATION_WINDOW_RADIUS),
             smoothnessPenaltySmall_(SGMSTEREO_DEFAULT_SMOOTHNESS_PENALTY_SMALL),
             smoothnessPenaltyLarge_(SGMSTEREO_DEFAULT_SMOOTHNESS_PENALTY_LARGE),
             consistencyThreshold_(SGMSTEREO_DEFAULT_CONSISTENCY_THRESHOLD) {}

void SGMStereo::setDisparityTotal(const int disparityTotal) {
  if (disparityTotal <= 0 || disparityTotal%16 != 0) {
    throw std::invalid_argument("[SGMStereo::setDisparityTotal] the number of disparities must be a multiple of 16");
  }

  disparityTotal_ = disparityTotal;
}

void SGMStereo::setDisparityFactor(const double disparityFactor) {
  if (disparityFactor <= 0) {
    throw std::invalid_argument("[SGMStereo::setOutputDisparityFactor] disparity factor is less than zero");
  }

  disparityFactor_ = disparityFactor;
}

void SGMStereo::setDataCostParameters(const int sobelCapValue,
                    const int censusWindowRadius,
                    const double censusWeightFactor,
                    const int aggregationWindowRadius)
{
  sobelCapValue_ = std::max(sobelCapValue, 15);
  sobelCapValue_ = std::min(sobelCapValue_, 127) | 1;

  if (censusWindowRadius < 1 || censusWindowRadius > 2) {
    throw std::invalid_argument("[SGMStereo::setDataCostParameters] window radius of Census transform must be 1 or 2");
  }
  censusWindowRadius_ = censusWindowRadius;
  if (censusWeightFactor < 0) {
    throw std::invalid_argument("[SGMStereo::setDataCostParameters] weight of Census transform must be positive");
  }
  censusWeightFactor_ = censusWeightFactor;

  aggregationWindowRadius_ = aggregationWindowRadius;
}

void SGMStereo::setSmoothnessCostParameters(const int smoothnessPenaltySmall, const int smoothnessPenaltyLarge)
{
  if (smoothnessPenaltySmall < 0 || smoothnessPenaltyLarge < 0) {
    throw std::invalid_argument("[SGMStereo::setSmoothnessCostParameters] smoothness penalty value is less than zero");
  }
  if (smoothnessPenaltySmall >= smoothnessPenaltyLarge) {
    throw std::invalid_argument("[SGMStereo::setSmoothnessCostParameters] small value of smoothness penalty must be smaller than large penalty value");
  }

  smoothnessPenaltySmall_ = smoothnessPenaltySmall;
  smoothnessPenaltyLarge_ = smoothnessPenaltyLarge;
}

void SGMStereo::setConsistencyThreshold(const int consistencyThreshold) {
  if (consistencyThreshold < 0) {
 
    throw std::invalid_argument("[SGMStereo::setConsistencyThreshold] threshold for LR consistency must be positive");
  }
  consistencyThreshold_ = consistencyThreshold;
}


void SGMStereo::meshgrid(const cv::Mat &xgv, const cv::Mat &ygv, cv::Mat1i &X, cv::Mat1i &Y)
{
  cv::repeat(xgv.reshape(1,1), ygv.total(), 1, X);
  cv::repeat(ygv.reshape(1,1).t(), 1, xgv.total(), Y);
}

void SGMStereo::meshgridInit(const cv::Range &xgv, const cv::Range &ygv, cv::Mat1i &X, cv::Mat1i &Y)
{
  std::vector<int> t_x, t_y;
  for (int i = xgv.start; i <= xgv.end; i++) t_x.push_back(i);
  for (int i = ygv.start; i <= ygv.end; i++) t_y.push_back(i);
  SGMStereo::meshgrid(cv::Mat(t_x), cv::Mat(t_y), X, Y);
}

/* NAIVE FUSION METHOD */
void SGMStereo::updateCostVolumes_NRF(unsigned short *leftCostVolume,
                                      unsigned short * rightCostVolume,
                                      cv::Mat leftImage,
                                      cv::Mat weightImage)

{
  std::ignore = rightCostVolume;
  std::ignore = leftImage;
  std::ignore = weightImage;
  unsigned short *l_temporaryVolume = new unsigned short[width_*height_*disparityTotal_];
  l_temporaryVolume = leftCostVolume;
  for (int r = 0; r < rangeMeasurement_.rows; ++r)
  {
    for (int c = 0; c < rangeMeasurement_.cols; ++c)
    {
      double disparityFromMonstar = rangeMeasurement_.at<double>(r,c);
      if (rangeMeasurement_.at<double>(r,c) > 0 )      
            {
              for (int dIdx = 0; dIdx < disparityTotal_; dIdx++) {
                int dispIdx = round(disparityFromMonstar);
                dispIdx = std::min(disparityTotal_ - 1, dispIdx);
                dispIdx = std::max(0, dispIdx);
                if (dispIdx == dIdx) {
                  l_temporaryVolume[r*rangeMeasurement_.cols*disparityTotal_ + c*disparityTotal_ + dIdx] = 0;
          }
        }
      }
    }
  }
  leftCostImage_ = l_temporaryVolume;
}



/* CROSS BASE METHOD */
void SGMStereo::updateCostVolumes_CB(unsigned short *leftCostVolume,
                                     unsigned short * rightCostVolume,
                                     cv::Mat leftImage,
                                     cv::Mat weightImage)

{
  std::ignore = rightCostVolume;
  std::ignore = leftImage;
  std::ignore = weightImage;
  unsigned short *l_temporaryVolume = new unsigned short[width_*height_*disparityTotal_];
  l_temporaryVolume = leftCostVolume;
  for (int r = 0; r < rangeMeasurement_.rows; ++r)
  {
    for (int c = 0; c < rangeMeasurement_.cols; ++c)
    {
      if (rangeMeasurement_.at<double>(r,c) > 0 )            
            {
              int ref_val = leftImage.at<unsigned char>(r,c);
              int rmin=0, rmax=0, cmin=0, cmax=0;
            
              int x = r;
              int y = c;
              int color0 = ref_val;  
              auto color_last = ref_val;
              int discolor1 = 8;
              int discolor2 = 3;
              int discolor3 = 1;
              
              sint32 dir = -1;
              for (sint32 k = 0; k < 2; k++) {
               
              
                sint32 xn = x + dir;                             
                for (sint32 n = 0; n < std::min(30, 255); n++) { 
                 
                  if (k == 0) {               
                    if (xn < 0) {           
                      break;
                    }
                  }
                  else {
                    if (xn == width_) {     
                      break;
                    }
                  }
                 
                int color = leftImage.at<unsigned char>(xn,y);
                  
                  sint32 color_dist1 = abs(color-color0);
                  if (color_dist1 >= discolor1) {
                    break;
                  }

                  if (n > 0) {
                    sint32 color_dist2 = abs(color-color_last);
                    if (color_dist2 >= discolor2) {
                      break;
                    }
                  }

                 
                  if (n + 1 > 20) {
                    if (color_dist1 >= discolor3) {
                      break;
                    }
                  }

                  if (k == 0) {         
                    rmin++;
                  }
                  else {                
                    rmax++;
                  }
                  color_last = color;   
                  xn += dir;			  
                }
                dir = -dir;
              }


              color0 = ref_val;  
              color_last = ref_val;
             
              dir = -1;
              for (sint32 k = 0; k < 2; k++) {
                
                sint32 yn = y + dir;
                for (sint32 n = 0; n < std::min(30, 255); n++) {

                 
                  if (k == 0) {
                    if (yn < 0) {
                      break;
                    }
                  }
                  else {
                    if (yn == height_) {
                      break;
                    }
                  }

                 
                  int color = leftImage.at<unsigned char>(x,yn);
                  
                  const sint32 color_dist1 = abs(color-color0);
                  if (color_dist1 >= discolor1) {
                    break;
                  }

                  
                  if (n > 0) {
                    const sint32 color_dist2 = abs(color-color_last);
                    if (color_dist2 >= discolor2) {
                      break;
                    }
                  }

                 
                  if (n + 1 > 30) {
                    if (color_dist1 >= discolor3) {
                      break;
                    }
                  }

                  if (k == 0) {
                    cmin++;
                  }
                  else {
                    cmax++;
                  }
                  color_last = color;
                  yn += dir;
                }
                dir = -dir;
              }

              rmin = r-rmin;
              rmax = r+rmax+1;
              cmin = c-cmin;
              cmax = c+cmax+1;

              double disparityFromMonstar = rangeMeasurement_.at<double>(r,c);
              for (int dIdx = 0; dIdx < disparityTotal_; dIdx++) {
                int dispIdx = round(disparityFromMonstar);
                dispIdx = std::min(disparityTotal_ - 1, dispIdx);
                dispIdx = std::max(0, dispIdx);
                if (dispIdx == dIdx) {
                  for(int i = rmin; i < rmax; i++){
                    if( i > width_ || i< 0 ) {break;} 
                  l_temporaryVolume[i*rangeMeasurement_.cols*disparityTotal_ + c*disparityTotal_ + dIdx] = 0;}
                   for(int i = cmin; i < cmax; i++){
                     if(i > height_ || i< 0) {break;} 
                  l_temporaryVolume[r*rangeMeasurement_.cols*disparityTotal_ + i*disparityTotal_ + dIdx] = 0;}
          }
        }
      }
    }
  }
  leftCostImage_ = l_temporaryVolume;
  std::cout<<"CROSS BASED UPDATE OVER"<<std::endl;
}



void SGMStereo::compute(const png::image<png::rgb_pixel>& leftImage,  
                        const png::image<png::rgb_pixel>& rightImage, 
                        float* disparityImage,                        
                        int STEREO_MODE,
                        std::string paramFile,
                        cv::Mat depthImage,                           
                        int FUSE_FLAG,                                
                        cv::Mat leftImageFuse,                       
                        cv::Mat weightImage)                         
{
  std::ignore = paramFile;
  std::ignore = weightImage;
  initialize(leftImage, rightImage);  

  /* Logic switch for whether to use fusion or original stereo sgm */
  if (FUSE_FLAG == 1 || FUSE_FLAG == 2 ) {
    std::cout << "{range data prep} using FLAG = 1, 2, image already in disparity space.. " << std::endl;
    rangeMeasurement_ = depthImage.clone();
  } else if (FUSE_FLAG == -1) {
    std::cout << "{status} no fusion. Semi global matching only.." << std::endl;
  }

  
  computeCostImage(leftImage, rightImage);

  /* Logic switch for different fusion methods */
  if (FUSE_FLAG == 1) {
	std::cout << "{volume update: CB} updating cost volume..." << std::endl;
    /* Cross based */
    updateCostVolumes_CB(leftCostImage_,         
                         rightCostImage_,        
                         leftImageFuse,          
                         cv::Mat());            
  } else if (FUSE_FLAG == 2) {
    std::cout << "{volume update: NF} updating cost volume..." << std::endl;
    /* naive range fusion */
    updateCostVolumes_NRF(leftCostImage_,
                          rightCostImage_,
                          cv::Mat(),
                          cv::Mat());
  }  else if (FUSE_FLAG == -1) {
    /* no volume updates */
    std::cout << "{volume update: NIL} no updates to volume..." << std::endl;
  }
 
  unsigned short* leftDisparityImage = reinterpret_cast<unsigned short*>(malloc(width_*height_*sizeof(unsigned short)));
  
 
  performSGM(leftCostImage_, leftDisparityImage);

  if (STEREO_MODE == -1) {
    std::cout << "{stereo mode} full stereo pipeline.." << std::endl;
    unsigned short* rightDisparityImage = reinterpret_cast<unsigned short*>(malloc(width_*height_*sizeof(unsigned short)));
    performSGM(rightCostImage_, rightDisparityImage);
    enforceLeftRightConsistency(leftDisparityImage, rightDisparityImage);   
    free(rightDisparityImage);
  }
  
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      disparityImage[width_*y + x] = static_cast<float>(leftDisparityImage[width_*y + x]/disparityFactor_);
    }
  }

  freeDataBuffer();
  free(leftDisparityImage);
}



void SGMStereo::initialize(const png::image<png::rgb_pixel>& leftImage, const png::image<png::rgb_pixel>& rightImage) {
  setImageSize(leftImage, rightImage);
  allocateDataBuffer();
}

void SGMStereo::setImageSize(const png::image<png::rgb_pixel>& leftImage, const png::image<png::rgb_pixel>& rightImage) {
  width_ = static_cast<int>(leftImage.get_width());
  height_ = static_cast<int>(leftImage.get_height());
  if ((int)rightImage.get_width() != width_ || (int)rightImage.get_height() != height_) {
    throw std::invalid_argument("[SGMStereo::setImageSize] sizes of left and right images are different");
  }
  widthStep_ = width_ + 15 - (width_ - 1)%16;
}

void SGMStereo::allocateDataBuffer() {
  leftCostImage_ = reinterpret_cast<unsigned short*>(_mm_malloc(width_*height_*disparityTotal_*sizeof(unsigned short), 16));
  rightCostImage_ = reinterpret_cast<unsigned short*>(_mm_malloc(width_*height_*disparityTotal_*sizeof(unsigned short), 16));

  int pixelwiseCostRowBufferSize = width_*disparityTotal_;
  int rowAggregatedCostBufferSize = width_*disparityTotal_*(aggregationWindowRadius_*2 + 2);
  int halfPixelRightBufferSize = widthStep_;

  pixelwiseCostRow_ = reinterpret_cast<unsigned char*>(_mm_malloc(pixelwiseCostRowBufferSize*sizeof(unsigned char), 16));
  rowAggregatedCost_ = reinterpret_cast<unsigned short*>(_mm_malloc(rowAggregatedCostBufferSize*sizeof(unsigned short), 16));
  halfPixelRightMin_ = reinterpret_cast<unsigned char*>(_mm_malloc(halfPixelRightBufferSize*sizeof(unsigned char), 16));
  halfPixelRightMax_ = reinterpret_cast<unsigned char*>(_mm_malloc(halfPixelRightBufferSize*sizeof(unsigned char), 16));

  pathRowBufferTotal_ = 2;
  disparitySize_ = disparityTotal_ + 16;
  pathTotal_ = 8;
  pathDisparitySize_ = pathTotal_*disparitySize_;

  costSumBufferRowSize_ = width_*disparityTotal_;
  costSumBufferSize_ = costSumBufferRowSize_*height_;
  pathMinCostBufferSize_ = (width_ + 2)*pathTotal_;
  pathCostBufferSize_ = pathMinCostBufferSize_*disparitySize_;
  totalBufferSize_ = (pathMinCostBufferSize_ + pathCostBufferSize_)*pathRowBufferTotal_ + costSumBufferSize_ + 16;

  sgmBuffer_ = reinterpret_cast<short*>(_mm_malloc(totalBufferSize_*sizeof(short), 16));
}


void SGMStereo::freeDataBuffer() {
  _mm_free(leftCostImage_);
  _mm_free(rightCostImage_);
  _mm_free(pixelwiseCostRow_);
  _mm_free(rowAggregatedCost_);
  _mm_free(halfPixelRightMin_);
  _mm_free(halfPixelRightMax_);
  _mm_free(sgmBuffer_);
}

void SGMStereo::computeCostImage(const png::image<png::rgb_pixel>& leftImage, const png::image<png::rgb_pixel>& rightImage) {
  unsigned char* leftGrayscaleImage = reinterpret_cast<unsigned char*>(malloc(width_*height_*sizeof(unsigned char)));
  unsigned char* rightGrayscaleImage = reinterpret_cast<unsigned char*>(malloc(width_*height_*sizeof(unsigned char)));
  convertToGrayscale(leftImage, rightImage, leftGrayscaleImage, rightGrayscaleImage);

  memset(leftCostImage_, 0, width_*height_*disparityTotal_*sizeof(unsigned short));
  computeLeftCostImage(leftGrayscaleImage, rightGrayscaleImage,leftImage,rightImage);

  computeRightCostImage();

  free(leftGrayscaleImage);
  free(rightGrayscaleImage);
}


void SGMStereo::convertToGrayscale(const png::image<png::rgb_pixel>& leftImage,
                   const png::image<png::rgb_pixel>& rightImage,
                   unsigned char* leftGrayscaleImage,
                   unsigned char* rightGrayscaleImage) const
{
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      png::rgb_pixel pix = leftImage.get_pixel(x, y);
      leftGrayscaleImage[width_*y + x] = static_cast<unsigned char>(0.299*pix.red + 0.587*pix.green + 0.114*pix.blue + 0.5);
      pix = rightImage.get_pixel(x, y);
      rightGrayscaleImage[width_*y + x] = static_cast<unsigned char>(0.299*pix.red + 0.587*pix.green + 0.114*pix.blue + 0.5);
    }
  }
}


void SGMStereo::computeLeftCostImage(const unsigned char* leftGrayscaleImage, const unsigned char* rightGrayscaleImage,const png::image<png::rgb_pixel>& leftImage, const png::image<png::rgb_pixel>& rightImage) {
  unsigned char* leftSobelImage = reinterpret_cast<unsigned char*>(_mm_malloc(widthStep_*height_*sizeof(unsigned char), 16));
  unsigned char* rightSobelImage = reinterpret_cast<unsigned char*>(_mm_malloc(widthStep_*height_*sizeof(unsigned char), 16));
  computeCappedSobelImage(leftGrayscaleImage, false, leftSobelImage);
  computeCappedSobelImage(rightGrayscaleImage, true, rightSobelImage);

  int* leftCensusImage = reinterpret_cast<int*>(malloc(width_*height_*sizeof(int)));
  int* rightCensusImage = reinterpret_cast<int*>(malloc(width_*height_*sizeof(int)));
  computeCensusImage(leftGrayscaleImage, leftCensusImage);
  computeCensusImage(rightGrayscaleImage, rightCensusImage);

  unsigned char* leftSobelRow = leftSobelImage;
  unsigned char* rightSobelRow = rightSobelImage;
  int* leftCensusRow = leftCensusImage;
  int* rightCensusRow = rightCensusImage;
  unsigned short* costImageRow = leftCostImage_;
  calcTopRowCost(leftSobelRow, leftCensusRow,
           rightSobelRow, rightCensusRow,
           costImageRow);
  costImageRow += width_*disparityTotal_;
  calcRowCosts(leftSobelRow, leftCensusRow,
         rightSobelRow, rightCensusRow,
         costImageRow);

  _mm_free(leftSobelImage);
  _mm_free(rightSobelImage);
  free(leftCensusImage);
  free(rightCensusImage);
}


void SGMStereo::computeCappedSobelImage(const unsigned char* image, const bool horizontalFlip, unsigned char* sobelImage) const {
  memset(sobelImage, sobelCapValue_, widthStep_*height_);

  if (horizontalFlip) {
    for (int y = 1; y < height_ - 1; ++y) {
      for (int x = 1; x < width_ - 1; ++x) {
        int sobelValue = (image[width_*(y - 1) + x + 1] + 2*image[width_*y + x + 1] + image[width_*(y + 1) + x + 1])
          - (image[width_*(y - 1) + x - 1] + 2*image[width_*y + x - 1] + image[width_*(y + 1) + x - 1]);
        if (sobelValue > sobelCapValue_) sobelValue = 2*sobelCapValue_;
        else if (sobelValue < -sobelCapValue_) sobelValue = 0;
        else sobelValue += sobelCapValue_;
        sobelImage[widthStep_*y + width_ - x - 1] = sobelValue;
      }
    }
  } else {
    for (int y = 1; y < height_ - 1; ++y) {
      for (int x = 1; x < width_ - 1; ++x) {
        int sobelValue = (image[width_*(y - 1) + x + 1] + 2*image[width_*y + x + 1] + image[width_*(y + 1) + x + 1])
          - (image[width_*(y - 1) + x - 1] + 2*image[width_*y + x - 1] + image[width_*(y + 1) + x - 1]);
        if (sobelValue > sobelCapValue_) sobelValue = 2*sobelCapValue_;
        else if (sobelValue < -sobelCapValue_) sobelValue = 0;
        else sobelValue += sobelCapValue_;
        sobelImage[widthStep_*y + x] = sobelValue;
      }
    }
  }
}

void SGMStereo::computeCensusImage(const unsigned char* image, int* censusImage) const {
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      unsigned char centerValue = image[width_*y + x];

      int censusCode = 0;
      for (int offsetY = -censusWindowRadius_; offsetY <= censusWindowRadius_; ++offsetY) {
        for (int offsetX = -censusWindowRadius_; offsetX <= censusWindowRadius_; ++offsetX) {
          censusCode = censusCode << 1;
          if (y + offsetY >= 0 && y + offsetY < height_
            && x + offsetX >= 0 && x + offsetX < width_
            && image[width_*(y + offsetY) + x + offsetX] >= centerValue) censusCode += 1; // compare to centerPixel
        }
      }
      censusImage[width_*y + x] = censusCode;
    }
  }
}


void SGMStereo::calcTopRowCost(unsigned char*& leftSobelRow, int*& leftCensusRow,
                 unsigned char*& rightSobelRow, int*& rightCensusRow,
                 unsigned short* costImageRow)
{
  for (int rowIndex = 0; rowIndex <= aggregationWindowRadius_; ++rowIndex) {
    int rowAggregatedCostIndex = std::min(rowIndex, height_ - 1)%(aggregationWindowRadius_*2 + 2);
    unsigned short* rowAggregatedCostCurrent = rowAggregatedCost_ + rowAggregatedCostIndex*width_*disparityTotal_;

    calcPixelwiseSAD(leftSobelRow, rightSobelRow);
    addPixelwiseHamming(leftCensusRow, rightCensusRow);

    memset(rowAggregatedCostCurrent, 0, disparityTotal_*sizeof(unsigned short));
    // x = 0
    for (int x = 0; x <= aggregationWindowRadius_; ++x) {
      int scale = x == 0 ? aggregationWindowRadius_ + 1 : 1;
      for (int d = 0; d < disparityTotal_; ++d) {
        rowAggregatedCostCurrent[d] += static_cast<unsigned short>(pixelwiseCostRow_[disparityTotal_*x + d]*scale);
      }
    }
    // x = 1...width-1
    for (int x = 1; x < width_; ++x) {
      const unsigned char* addPixelwiseCost = pixelwiseCostRow_
        + std::min((x + aggregationWindowRadius_)*disparityTotal_, (width_ - 1)*disparityTotal_);
      const unsigned char* subPixelwiseCost = pixelwiseCostRow_
        + std::max((x - aggregationWindowRadius_ - 1)*disparityTotal_, 0);

      for (int d = 0; d < disparityTotal_; ++d) {
        rowAggregatedCostCurrent[disparityTotal_*x + d]
          = static_cast<unsigned short>(rowAggregatedCostCurrent[disparityTotal_*(x - 1) + d]
          + addPixelwiseCost[d] - subPixelwiseCost[d]);
      }
    }

    
    int scale = rowIndex == 0 ? aggregationWindowRadius_ + 1 : 1;
    for (int i = 0; i < width_*disparityTotal_; ++i) {
      costImageRow[i] += rowAggregatedCostCurrent[i]*scale;
    }

    leftSobelRow += widthStep_;
    rightSobelRow += widthStep_;
    leftCensusRow += width_;
    rightCensusRow += width_;
  }
}


void SGMStereo::calcRowCosts(unsigned char*& leftSobelRow, int*& leftCensusRow,
               unsigned char*& rightSobelRow, int*& rightCensusRow,
               unsigned short* costImageRow)
{
  const int widthStepCost = width_*disparityTotal_;
  const __m128i registerZero = _mm_setzero_si128();

  for (int y = 1; y < height_; ++y) {
    int addRowIndex = y + aggregationWindowRadius_;
    int addRowAggregatedCostIndex = std::min(addRowIndex, height_ - 1)%(aggregationWindowRadius_*2 + 2);
    unsigned short* addRowAggregatedCost = rowAggregatedCost_ + width_*disparityTotal_*addRowAggregatedCostIndex;

    if (addRowIndex < height_) {
      calcPixelwiseSAD(leftSobelRow, rightSobelRow);
      addPixelwiseHamming(leftCensusRow, rightCensusRow);

      memset(addRowAggregatedCost, 0, disparityTotal_*sizeof(unsigned short));
      // x = 0
      for (int x = 0; x <= aggregationWindowRadius_; ++x) {
        int scale = x == 0 ? aggregationWindowRadius_ + 1 : 1;
        for (int d = 0; d < disparityTotal_; ++d) {
          addRowAggregatedCost[d] += static_cast<unsigned short>(pixelwiseCostRow_[disparityTotal_*x + d]*scale);
        }
      }
      // x = 1...width-1
      int subRowAggregatedCostIndex = std::max(y - aggregationWindowRadius_ - 1, 0)%(aggregationWindowRadius_*2 + 2);
      const unsigned short* subRowAggregatedCost = rowAggregatedCost_ + width_*disparityTotal_*subRowAggregatedCostIndex;
      const unsigned short* previousCostRow = costImageRow - widthStepCost;
      for (int x = 1; x < width_; ++x) {
        const unsigned char* addPixelwiseCost = pixelwiseCostRow_
          + std::min((x + aggregationWindowRadius_)*disparityTotal_, (width_ - 1)*disparityTotal_);
        const unsigned char* subPixelwiseCost = pixelwiseCostRow_
          + std::max((x - aggregationWindowRadius_ - 1)*disparityTotal_, 0);

        for (int d = 0; d < disparityTotal_; d += 16) {
          __m128i registerAddPixelwiseLow = _mm_load_si128(reinterpret_cast<const __m128i*>(addPixelwiseCost + d));
          __m128i registerAddPixelwiseHigh = _mm_unpackhi_epi8(registerAddPixelwiseLow, registerZero);
          registerAddPixelwiseLow = _mm_unpacklo_epi8(registerAddPixelwiseLow, registerZero);
          __m128i registerSubPixelwiseLow = _mm_load_si128(reinterpret_cast<const __m128i*>(subPixelwiseCost + d));
          __m128i registerSubPixelwiseHigh = _mm_unpackhi_epi8(registerSubPixelwiseLow, registerZero);
          registerSubPixelwiseLow = _mm_unpacklo_epi8(registerSubPixelwiseLow, registerZero);

          // Low
          __m128i registerAddAggregated = _mm_load_si128(reinterpret_cast<const __m128i*>(addRowAggregatedCost
            + disparityTotal_*(x - 1) + d));
          registerAddAggregated = _mm_adds_epi16(_mm_subs_epi16(registerAddAggregated, registerSubPixelwiseLow),
                               registerAddPixelwiseLow);
          __m128i registerCost = _mm_load_si128(reinterpret_cast<const __m128i*>(previousCostRow + disparityTotal_*x + d));
          registerCost = _mm_adds_epi16(_mm_subs_epi16(registerCost,
            _mm_load_si128(reinterpret_cast<const __m128i*>(subRowAggregatedCost + disparityTotal_*x + d))),
            registerAddAggregated);
          _mm_store_si128(reinterpret_cast<__m128i*>(addRowAggregatedCost + disparityTotal_*x + d), registerAddAggregated);
          _mm_store_si128(reinterpret_cast<__m128i*>(costImageRow + disparityTotal_*x + d), registerCost);

          // High
          registerAddAggregated = _mm_load_si128(reinterpret_cast<const __m128i*>(addRowAggregatedCost + disparityTotal_*(x-1) + d + 8));
          registerAddAggregated = _mm_adds_epi16(_mm_subs_epi16(registerAddAggregated, registerSubPixelwiseHigh),
                               registerAddPixelwiseHigh);
          registerCost = _mm_load_si128(reinterpret_cast<const __m128i*>(previousCostRow + disparityTotal_*x + d + 8));
          registerCost = _mm_adds_epi16(_mm_subs_epi16(registerCost,
            _mm_load_si128(reinterpret_cast<const __m128i*>(subRowAggregatedCost + disparityTotal_*x + d + 8))),
            registerAddAggregated);
          _mm_store_si128(reinterpret_cast<__m128i*>(addRowAggregatedCost + disparityTotal_*x + d + 8), registerAddAggregated);
          _mm_store_si128(reinterpret_cast<__m128i*>(costImageRow + disparityTotal_*x + d + 8), registerCost);
        }
      }
    }

    leftSobelRow += widthStep_;
    rightSobelRow += widthStep_;
    leftCensusRow += width_;
    rightCensusRow += width_;
    costImageRow += widthStepCost;
  }
}

void SGMStereo::calcPixelwiseSAD(const unsigned char* leftSobelRow, const unsigned char* rightSobelRow) {
  calcHalfPixelRight(rightSobelRow);

  for (int x = 0; x < 16; ++x) {
    int leftCenterValue = leftSobelRow[x];
    int leftHalfLeftValue = x > 0 ? (leftCenterValue + leftSobelRow[x - 1])/2 : leftCenterValue;
    int leftHalfRightValue = x < width_ - 1 ? (leftCenterValue + leftSobelRow[x + 1])/2 : leftCenterValue;
    int leftMinValue = std::min(leftHalfLeftValue, leftHalfRightValue);
    leftMinValue = std::min(leftMinValue, leftCenterValue);
    int leftMaxValue = std::max(leftHalfLeftValue, leftHalfRightValue);
    leftMaxValue = std::max(leftMaxValue, leftCenterValue);

    for (int d = 0; d <= x; ++d) {
      int rightCenterValue = rightSobelRow[width_ - 1 - x + d];
      int rightMinValue = halfPixelRightMin_[width_ - 1 - x + d];
      int rightMaxValue = halfPixelRightMax_[width_ - 1 - x + d];

      int costLtoR = std::max(0, leftCenterValue - rightMaxValue);
      costLtoR = std::max(costLtoR, rightMinValue - leftCenterValue);
      int costRtoL = std::max(0, rightCenterValue - leftMaxValue);
      costRtoL = std::max(costRtoL, leftMinValue - rightCenterValue);
      int costValue = std::min(costLtoR, costRtoL);

      pixelwiseCostRow_[disparityTotal_*x + d] = costValue;
    }
    for (int d = x + 1; d < disparityTotal_; ++d) {
      pixelwiseCostRow_[disparityTotal_*x + d] = pixelwiseCostRow_[disparityTotal_*x + d - 1];
    }
  }
  for (int x = 16; x < disparityTotal_; ++x) {
    int leftCenterValue = leftSobelRow[x];
    int leftHalfLeftValue = x > 0 ? (leftCenterValue + leftSobelRow[x - 1])/2 : leftCenterValue;
    int leftHalfRightValue = x < width_ - 1 ? (leftCenterValue + leftSobelRow[x + 1])/2 : leftCenterValue;
    int leftMinValue = std::min(leftHalfLeftValue, leftHalfRightValue);
    leftMinValue = std::min(leftMinValue, leftCenterValue);
    int leftMaxValue = std::max(leftHalfLeftValue, leftHalfRightValue);
    leftMaxValue = std::max(leftMaxValue, leftCenterValue);

    __m128i registerLeftCenterValue = _mm_set1_epi8(static_cast<char>(leftCenterValue));
    __m128i registerLeftMinValue = _mm_set1_epi8(static_cast<char>(leftMinValue));
    __m128i registerLeftMaxValue = _mm_set1_epi8(static_cast<char>(leftMaxValue));

    for (int d = 0; d < x/16; d += 16) {
      __m128i registerRightCenterValue = _mm_loadu_si128(reinterpret_cast<const __m128i*>(rightSobelRow + width_ - 1 - x + d));
      __m128i registerRightMinValue = _mm_loadu_si128(reinterpret_cast<const __m128i*>(halfPixelRightMin_ + width_ - 1 - x + d));
      __m128i registerRightMaxValue = _mm_loadu_si128(reinterpret_cast<const __m128i*>(halfPixelRightMax_ + width_ - 1 - x + d));

      __m128i registerCostLtoR = _mm_max_epu8(_mm_subs_epu8(registerLeftCenterValue, registerRightMaxValue),
                          _mm_subs_epu8(registerRightMinValue, registerLeftCenterValue));
      __m128i registerCostRtoL = _mm_max_epu8(_mm_subs_epu8(registerRightCenterValue, registerLeftMaxValue),
                          _mm_subs_epu8(registerLeftMinValue, registerRightCenterValue));
      __m128i registerCost = _mm_min_epu8(registerCostLtoR, registerCostRtoL);

      _mm_store_si128(reinterpret_cast<__m128i*>(pixelwiseCostRow_ + disparityTotal_*x + d), registerCost);
    }
    for (int d = x/16; d <= x; ++d) {
      int rightCenterValue = rightSobelRow[width_ - 1 - x + d];
      int rightMinValue = halfPixelRightMin_[width_ - 1 - x + d];
      int rightMaxValue = halfPixelRightMax_[width_ - 1 - x + d];

      int costLtoR = std::max(0, leftCenterValue - rightMaxValue);
      costLtoR = std::max(costLtoR, rightMinValue - leftCenterValue);
      int costRtoL = std::max(0, rightCenterValue - leftMaxValue);
      costRtoL = std::max(costRtoL, leftMinValue - rightCenterValue);
      int costValue = std::min(costLtoR, costRtoL);

      pixelwiseCostRow_[disparityTotal_*x + d] = costValue;
    }
    for (int d = x + 1; d < disparityTotal_; ++d) {
      pixelwiseCostRow_[disparityTotal_*x + d] = pixelwiseCostRow_[disparityTotal_*x + d - 1];
    }
  }
  for (int x = disparityTotal_; x < width_; ++x) {
    int leftCenterValue = leftSobelRow[x];
    int leftHalfLeftValue = x > 0 ? (leftCenterValue + leftSobelRow[x - 1])/2 : leftCenterValue;
    int leftHalfRightValue = x < width_ - 1 ? (leftCenterValue + leftSobelRow[x + 1])/2 : leftCenterValue;
    int leftMinValue = std::min(leftHalfLeftValue, leftHalfRightValue);
    leftMinValue = std::min(leftMinValue, leftCenterValue);
    int leftMaxValue = std::max(leftHalfLeftValue, leftHalfRightValue);
    leftMaxValue = std::max(leftMaxValue, leftCenterValue);

    __m128i registerLeftCenterValue = _mm_set1_epi8(static_cast<char>(leftCenterValue));
    __m128i registerLeftMinValue = _mm_set1_epi8(static_cast<char>(leftMinValue));
    __m128i registerLeftMaxValue = _mm_set1_epi8(static_cast<char>(leftMaxValue));

    for (int d = 0; d < disparityTotal_; d += 16) {
      __m128i registerRightCenterValue = _mm_loadu_si128(reinterpret_cast<const __m128i*>(rightSobelRow + width_ - 1 - x + d));
      __m128i registerRightMinValue = _mm_loadu_si128(reinterpret_cast<const __m128i*>(halfPixelRightMin_ + width_ - 1 - x + d));
      __m128i registerRightMaxValue = _mm_loadu_si128(reinterpret_cast<const __m128i*>(halfPixelRightMax_ + width_ - 1 - x + d));

      __m128i registerCostLtoR = _mm_max_epu8(_mm_subs_epu8(registerLeftCenterValue, registerRightMaxValue),
                          _mm_subs_epu8(registerRightMinValue, registerLeftCenterValue));
      __m128i registerCostRtoL = _mm_max_epu8(_mm_subs_epu8(registerRightCenterValue, registerLeftMaxValue),
                          _mm_subs_epu8(registerLeftMinValue, registerRightCenterValue));
      __m128i registerCost = _mm_min_epu8(registerCostLtoR, registerCostRtoL);

      _mm_store_si128(reinterpret_cast<__m128i*>(pixelwiseCostRow_ + disparityTotal_*x + d), registerCost);
    }
  }
}

void SGMStereo::calcHalfPixelRight(const unsigned char* rightSobelRow) {
  for (int x = 0; x < width_; ++x) {
    int centerValue = rightSobelRow[x];
    int leftHalfValue = x > 0 ? (centerValue + rightSobelRow[x - 1])/2 : centerValue;
    int rightHalfValue = x < width_ - 1 ? (centerValue + rightSobelRow[x + 1])/2 : centerValue;
    int minValue = std::min(leftHalfValue, rightHalfValue);
    minValue = std::min(minValue, centerValue);
    int maxValue = std::max(leftHalfValue, rightHalfValue);
    maxValue = std::max(maxValue, centerValue);

    halfPixelRightMin_[x] = minValue;
    halfPixelRightMax_[x] = maxValue;
  }
}

void SGMStereo::addPixelwiseHamming(const int* leftCensusRow, const int* rightCensusRow) {
  for (int x = 0; x < disparityTotal_; ++x) {
    int leftCencusCode = leftCensusRow[x];
    int hammingDistance = 0;
    for (int d = 0; d <= x; ++d) {
      int rightCensusCode = rightCensusRow[x - d];
      hammingDistance = static_cast<int>(_mm_popcnt_u32(static_cast<unsigned int>(leftCencusCode^rightCensusCode)));
      pixelwiseCostRow_[disparityTotal_*x + d] += static_cast<unsigned char>(hammingDistance*censusWeightFactor_);
    }
    hammingDistance = static_cast<unsigned char>(hammingDistance*censusWeightFactor_);
    for (int d = x + 1; d < disparityTotal_; ++d) {
      pixelwiseCostRow_[disparityTotal_*x + d] += hammingDistance;
    }
  }
  for (int x = disparityTotal_; x < width_; ++x) {
    int leftCencusCode = leftCensusRow[x];
    for (int d = 0; d < disparityTotal_; ++d) {
      int rightCensusCode = rightCensusRow[x - d];
      int hammingDistance = static_cast<int>(_mm_popcnt_u32(static_cast<unsigned int>(leftCencusCode^rightCensusCode)));
      pixelwiseCostRow_[disparityTotal_*x + d] += static_cast<unsigned char>(hammingDistance*censusWeightFactor_);
    }
  }
}

void SGMStereo::computeRightCostImage() {
  const int widthStepCost = width_*disparityTotal_;

  for (int y = 0; y < height_; ++y) {
    unsigned short* leftCostRow = leftCostImage_ + widthStepCost*y;
    unsigned short* rightCostRow = rightCostImage_ + widthStepCost*y;

    for (int x = 0; x < disparityTotal_; ++x) {
      unsigned short* leftCostPointer = leftCostRow + disparityTotal_*x;
      unsigned short* rightCostPointer = rightCostRow + disparityTotal_*x;
      for (int d = 0; d <= x; ++d) {
        *(rightCostPointer) = *(leftCostPointer);
        rightCostPointer -= disparityTotal_ - 1;
        ++leftCostPointer;
      }
    }

    for (int x = disparityTotal_; x < width_; ++x) {
      unsigned short* leftCostPointer = leftCostRow + disparityTotal_*x;
      unsigned short* rightCostPointer = rightCostRow + disparityTotal_*x;
      for (int d = 0; d < disparityTotal_; ++d) {
        *(rightCostPointer) = *(leftCostPointer);
        rightCostPointer -= disparityTotal_ - 1;
        ++leftCostPointer;
      }
    }

    for (int x = width_ - disparityTotal_ + 1; x < width_; ++x) {
      int maxDisparityIndex = width_ - x;
      unsigned short lastValue = *(rightCostRow + disparityTotal_*x + maxDisparityIndex - 1);

      unsigned short* rightCostPointer = rightCostRow + disparityTotal_*x + maxDisparityIndex;
      for (int d = maxDisparityIndex; d < disparityTotal_; ++d) {
        *(rightCostPointer) = lastValue;
        ++rightCostPointer;
      }
    }
  }
}




void SGMStereo::performSGM(unsigned short* costImage, unsigned short* disparityImage) {
  const short costMax = SHRT_MAX;

  int widthStepCostImage = width_*disparityTotal_;

  short* costSums = sgmBuffer_;
  memset(costSums, 0, costSumBufferSize_*sizeof(short));

  short** pathCosts = new short*[pathRowBufferTotal_];
  short** pathMinCosts = new short*[pathRowBufferTotal_];

  const int processPassTotal = 2;
  for (int processPassCount = 0; processPassCount < processPassTotal; ++processPassCount) {
    int startX, endX, stepX;
    int startY, endY, stepY;
    if (processPassCount == 0) {
      // forward pass (0,0) -> (height_,width_)
      startX = 0; endX = width_; stepX = 1;
      startY = 0; endY = height_; stepY = 1;
    } else {
      // backward pass (height_, width_)
      startX = width_ - 1; endX = -1; stepX = -1;
      startY = height_ - 1; endY = -1; stepY = -1;
    }

    // allocate cost buffers
    for (int i = 0; i < pathRowBufferTotal_; ++i) {
      pathCosts[i] = costSums + costSumBufferSize_ + pathCostBufferSize_*i + pathDisparitySize_ + 8;
      memset(pathCosts[i] - pathDisparitySize_ - 8, 0, pathCostBufferSize_*sizeof(short));
      pathMinCosts[i] = costSums + costSumBufferSize_ + pathCostBufferSize_*pathRowBufferTotal_
        + pathMinCostBufferSize_*i + pathTotal_*2;
      memset(pathMinCosts[i] - pathTotal_, 0, pathMinCostBufferSize_*sizeof(short));
    }

    // iterate over image rows
    for (int y = startY; y != endY; y += stepY) {
      unsigned short* pixelCostRow = costImage + widthStepCostImage*y;
      short* costSumRow = costSums + costSumBufferRowSize_*y;

      memset(pathCosts[0] - pathDisparitySize_ - 8, 0, pathDisparitySize_*sizeof(short));
      memset(pathCosts[0] + width_*pathDisparitySize_ - 8, 0, pathDisparitySize_*sizeof(short));
      memset(pathMinCosts[0] - pathTotal_, 0, pathTotal_*sizeof(short));
      memset(pathMinCosts[0] + width_*pathTotal_, 0, pathTotal_*sizeof(short));

      // iterate over image columns
      for (int x = startX; x != endX; x += stepX) {
        int pathMinX = x*pathTotal_;
        int pathX = pathMinX*disparitySize_;

        int previousPathMin0 = pathMinCosts[0][pathMinX - stepX*pathTotal_] + smoothnessPenaltyLarge_;
        int previousPathMin2 = pathMinCosts[1][pathMinX + 2] + smoothnessPenaltyLarge_;

        short* previousPathCosts0 = pathCosts[0] + pathX - stepX*pathDisparitySize_;
        short* previousPathCosts2 = pathCosts[1] + pathX + disparitySize_*2;

        previousPathCosts0[-1] = previousPathCosts0[disparityTotal_] = costMax;
        previousPathCosts2[-1] = previousPathCosts2[disparityTotal_] = costMax;

        short* pathCostCurrent = pathCosts[0] + pathX;
        const unsigned short* pixelCostCurrent = pixelCostRow + disparityTotal_*x;
        short* costSumCurrent = costSumRow + disparityTotal_*x;

        __m128i regPenaltySmall = _mm_set1_epi16(static_cast<short>(smoothnessPenaltySmall_));

        __m128i regPathMin0, regPathMin2;
        regPathMin0 = _mm_set1_epi16(static_cast<short>(previousPathMin0));
        regPathMin2 = _mm_set1_epi16(static_cast<short>(previousPathMin2));
        __m128i regNewPathMin = _mm_set1_epi16(costMax);

        // iterate over disparity layers
        for (int d = 0; d < disparityTotal_; d += 8) {
          __m128i regPixelCost = _mm_load_si128(reinterpret_cast<const __m128i*>(pixelCostCurrent + d));

          __m128i regPathCost0, regPathCost2;
          regPathCost0 = _mm_load_si128(reinterpret_cast<const __m128i*>(previousPathCosts0 + d));
          regPathCost2 = _mm_load_si128(reinterpret_cast<const __m128i*>(previousPathCosts2 + d));

          regPathCost0 = _mm_min_epi16(regPathCost0,
                         _mm_adds_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(previousPathCosts0 + d - 1)),
                         regPenaltySmall));
          regPathCost0 = _mm_min_epi16(regPathCost0,
                         _mm_adds_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(previousPathCosts0 + d + 1)),
                         regPenaltySmall));
          regPathCost2 = _mm_min_epi16(regPathCost2,
                         _mm_adds_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(previousPathCosts2 + d - 1)),
                         regPenaltySmall));
          regPathCost2 = _mm_min_epi16(regPathCost2,
                         _mm_adds_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i*>(previousPathCosts2 + d + 1)),
                         regPenaltySmall));

          regPathCost0 = _mm_min_epi16(regPathCost0, regPathMin0);
          regPathCost0 = _mm_adds_epi16(_mm_subs_epi16(regPathCost0, regPathMin0), regPixelCost);
          regPathCost2 = _mm_min_epi16(regPathCost2, regPathMin2);
          regPathCost2 = _mm_adds_epi16(_mm_subs_epi16(regPathCost2, regPathMin2), regPixelCost);

          _mm_store_si128(reinterpret_cast<__m128i*>(pathCostCurrent + d), regPathCost0);
          _mm_store_si128(reinterpret_cast<__m128i*>(pathCostCurrent + d + disparitySize_*2), regPathCost2);

          __m128i regMin02 = _mm_min_epi16(_mm_unpacklo_epi16(regPathCost0, regPathCost2),
                           _mm_unpackhi_epi16(regPathCost0, regPathCost2));
          regMin02 = _mm_min_epi16(_mm_unpacklo_epi16(regMin02, regMin02),
                       _mm_unpackhi_epi16(regMin02, regMin02));
          regNewPathMin = _mm_min_epi16(regNewPathMin, regMin02);

          __m128i regCostSum = _mm_load_si128(reinterpret_cast<const __m128i*>(costSumCurrent + d));

          regCostSum = _mm_adds_epi16(regCostSum, regPathCost0);
          regCostSum = _mm_adds_epi16(regCostSum, regPathCost2);

          _mm_store_si128(reinterpret_cast<__m128i*>(costSumCurrent + d), regCostSum);
        }

        regNewPathMin = _mm_min_epi16(regNewPathMin, _mm_srli_si128(regNewPathMin, 8));
        _mm_storel_epi64(reinterpret_cast<__m128i*>(&pathMinCosts[0][pathMinX]), regNewPathMin);
      }

      // last pass: calculate final min_idxs
      if (processPassCount == processPassTotal - 1) {
        unsigned short* disparityRow = disparityImage + width_*y;

        for (int x = 0; x < width_; ++x) {
          short* costSumCurrent = costSumRow + disparityTotal_*x;
          int bestSumCost = costSumCurrent[0];
          int bestDisparity = 0;
          for (int d = 1; d < disparityTotal_; ++d) {
            if (costSumCurrent[d] < bestSumCost) {
              bestSumCost = costSumCurrent[d];
              bestDisparity = d;
            }
          }

          if (bestDisparity > 0 && bestDisparity < disparityTotal_ - 1) {
            int centerCostValue = costSumCurrent[bestDisparity];
            int leftCostValue = costSumCurrent[bestDisparity - 1];
            int rightCostValue = costSumCurrent[bestDisparity + 1];
            if (rightCostValue < leftCostValue) {
              bestDisparity = static_cast<int>(bestDisparity*disparityFactor_
                               + static_cast<double>(rightCostValue - leftCostValue)/(centerCostValue - leftCostValue)/2.0*disparityFactor_ + 0.5);
            } else {
              bestDisparity = static_cast<int>(bestDisparity*disparityFactor_
                               + static_cast<double>(rightCostValue - leftCostValue)/(centerCostValue - rightCostValue)/2.0*disparityFactor_ + 0.5);
            }
          } else {
            bestDisparity = static_cast<int>(bestDisparity*disparityFactor_);
          }

          disparityRow[x] = static_cast<unsigned short>(bestDisparity);
        }
      }

      std::swap(pathCosts[0], pathCosts[1]);
      std::swap(pathMinCosts[0], pathMinCosts[1]);
    }
  }
  delete[] pathCosts;
  delete[] pathMinCosts;

}

void SGMStereo::speckleFilter(const int maxSpeckleSize, const int maxDifference, unsigned short* image) const {
  std::vector<int> labels(width_*height_, 0);
  std::vector<bool> regionTypes(1);
  regionTypes[0] = false;

  int currentLabelIndex = 0;

  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      int pixelIndex = width_*y + x;
      if (image[width_*y + x] != 0) {
        if (labels[pixelIndex] > 0) {
          if (regionTypes[labels[pixelIndex]]) {
            image[width_*y + x] = 0;
          }
        } else {
          std::stack<int> wavefrontIndices;
          wavefrontIndices.push(pixelIndex);
          ++currentLabelIndex;
          regionTypes.push_back(false);
          int regionPixelTotal = 0;
          labels[pixelIndex] = currentLabelIndex;

          while (!wavefrontIndices.empty()) {
            int currentPixelIndex = wavefrontIndices.top();
            wavefrontIndices.pop();
            int currentX = currentPixelIndex%width_;
            int currentY = currentPixelIndex/width_;
            ++regionPixelTotal;
            unsigned short pixelValue = image[width_*currentY + currentX];

            if (currentX < width_ - 1 && labels[currentPixelIndex + 1] == 0
              && image[width_*currentY + currentX + 1] != 0
              && std::abs(pixelValue - image[width_*currentY + currentX + 1]) <= maxDifference)
            {
              labels[currentPixelIndex + 1] = currentLabelIndex;
              wavefrontIndices.push(currentPixelIndex + 1);
            }

            if (currentX > 0 && labels[currentPixelIndex - 1] == 0
              && image[width_*currentY + currentX - 1] != 0
              && std::abs(pixelValue - image[width_*currentY + currentX - 1]) <= maxDifference)
            {
              labels[currentPixelIndex - 1] = currentLabelIndex;
              wavefrontIndices.push(currentPixelIndex - 1);
            }

            if (currentY < height_ - 1 && labels[currentPixelIndex + width_] == 0
              && image[width_*(currentY + 1) + currentX] != 0
              && std::abs(pixelValue - image[width_*(currentY + 1) + currentX]) <= maxDifference)
            {
              labels[currentPixelIndex + width_] = currentLabelIndex;
              wavefrontIndices.push(currentPixelIndex + width_);
            }

            if (currentY > 0 && labels[currentPixelIndex - width_] == 0
              && image[width_*(currentY - 1) + currentX] != 0
              && std::abs(pixelValue - image[width_*(currentY - 1) + currentX]) <= maxDifference)
            {
              labels[currentPixelIndex - width_] = currentLabelIndex;
              wavefrontIndices.push(currentPixelIndex - width_);
            }
          }

          if (regionPixelTotal <= maxSpeckleSize) {
            regionTypes[currentLabelIndex] = true;
            image[width_*y + x] = 0;
          }
        }
      }
    }
  }
}


void SGMStereo::enforceLeftRightConsistency(unsigned short* leftDisparityImage, unsigned short* rightDisparityImage) const {
  // Check left disparity image
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      if (leftDisparityImage[width_*y + x] == 0) continue;

      int leftDisparityValue = static_cast<int>(static_cast<double>(leftDisparityImage[width_*y + x])/disparityFactor_ + 0.5);
      if (x - leftDisparityValue < 0) {
        leftDisparityImage[width_*y + x] = 0;
        continue;
      }

      int rightDisparityValue = static_cast<int>(static_cast<double>(rightDisparityImage[width_*y + x-leftDisparityValue])/disparityFactor_ + 0.5);
      if (rightDisparityValue == 0 || abs(leftDisparityValue - rightDisparityValue) > consistencyThreshold_) {
        leftDisparityImage[width_*y + x] = 0;
      }
    }
  }

  // Check right disparity image
  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      if (rightDisparityImage[width_*y + x] == 0)  continue;

      int rightDisparityValue = static_cast<int>(static_cast<double>(rightDisparityImage[width_*y + x])/disparityFactor_ + 0.5);
      if (x + rightDisparityValue >= width_) {
        rightDisparityImage[width_*y + x] = 0;
        continue;
      }

      int leftDisparityValue = static_cast<int>(static_cast<double>(leftDisparityImage[width_*y + x+rightDisparityValue])/disparityFactor_ + 0.5);
      if (leftDisparityValue == 0 || abs(rightDisparityValue - leftDisparityValue) > consistencyThreshold_) {
        rightDisparityImage[width_*y + x] = 0;
      }
    }
  }
}
