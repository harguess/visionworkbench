#include <iostream>
#include <fstream>
#include <vw/Image.h>
#include <vw/Image/PixelMath.h>
#include <vw/Image/PixelMask.h>
#include <vw/Image/MaskViews.h>
#include <vw/FileIO.h>
#include <math.h>
#include <time.h>

//josh 
#include <stdio.h>
#include <stdlib.h>

#include <vw/Core.h>
#include <vw/Cartography.h>
#include <vw/Math.h>


#include <vw/Math/Matrix.h>

#include <vw/Math/LinearAlgebra.h>
#include <vw/Image/ImageViewRef.h>
#include <vw/Image/Interpolation.h>
#include <vw/Image/ImageMath.h>
#include <vw/Photometry/Reflectance.h>
#include <vw/Photometry/Weights.h>
#include <vw/Photometry/ReconstructError.h>

#include <cmath>
//#include <ctime>

using namespace std;
using namespace vw;
using namespace vw::cartography;

#include <math.h>
#include <vw/Photometry/Reconstruct.h>
#include <vw/Photometry/ShapeFromShading.h>
using namespace vw::photometry;

#define horBlockSize 16 //8 //4
#define verBlockSize 16 //8 //4
#define horExtendedSize (horBlockSize+1)
#define verExtendedSize (verBlockSize+1)
#define BlockArea (horBlockSize*verBlockSize) //size of both sides of the final jacobian matrix
#define ExtendedArea ((horExtendedSize)*(verExtendedSize))
#define AugmentedBlockArea (ExtendedArea - 1) //size of row side of the intermediate jacobian and size of intermediate error vector
#define temp 0 //temporary starting place to speed up files if necessary
#define UNIQUE "oldcode_olddem_nostage3"
#define writefiles 1 // 0 = no, 1 = yes

enum LossType { GAUSSIAN, CAUCHY, EXPONENTIAL };
double LOSS_ACCURACY_MULT = 1;

// LossType LOSS_VOLUME_TYPE = CAUCHY;
// double LOSS_VOLUME_MULT = 0.0001;
// double LOSS_VOLUME_SIGMA = 0.02;

LossType LOSS_VOLUME_TYPE = EXPONENTIAL;
double LOSS_VOLUME_MULT = 0.0005;
double LOSS_VOLUME_SIGMA = 0;


template <class T> T square(const T& x) { return x*x; }
template <class T> T sign(const T& x) { return (x > 0) - (x < 0); }


//compute the elements of the normal derivative
Vector3 ComputeNormalDerivative(int flag,  Vector3 xyz, Vector3 xyzTOP, Vector3 xyzLEFT)
{
  // xo = (xo0,xo1,xo2), xt = (xt0,xt1,xt2), xl = (xl0,xl1,xl2)
  // normal = cross_prod(xt-xo, xl-xo);
  // normal = [(xt1-xo1)*(xl2-xo2)-(xt2-xo2)*(xl1-xo1),
  //					 (xt2-xo2)*(xl0-xo0)-(xt0-xo0)*(xl2-xo2),
  //					 (xt0-xo0)*(xl1-xo1)-(xt1-xo1)*(xl0-xo0)]
  // d_n/xo2 = (xl1-xt1,xt0-xl0,0)
  // d_n/xl2 = (xt1-xo1,xo0-xt0,0)
  // d_n/xt2 = (xo1-xl1,xl0-xo0,0)

  Vector3 normalDerivative;
  if (flag == 0){ //wrt z_{i,j} //Current
    normalDerivative(0) = xyzLEFT(1) - xyzTOP(1); //dn_x/dz_{ij}
    normalDerivative(1) = xyzTOP(0) - xyzLEFT(0); //dn_y/dz_{ij}
    normalDerivative(2) = 0; //dnz_dz_{ij}
  }
  if (flag == 1){ //wrt z_{i-1,j} //LEFT
    normalDerivative(0) = xyzTOP(1) - xyz(1); //dn_x/dz_{i-1, j}
    normalDerivative(1) = xyz(0) - xyzTOP(0); //dn_y/dz_{i-1,j}
    normalDerivative(2) = 0; //dnz_dz_{i-1,j}
  }
  if (flag == 2){ //wrt z_{i,j-1} //TOP
    normalDerivative(0) = xyz(1) - xyzLEFT(1); //dn_x/dz_{i, j-1}
    normalDerivative(1) = xyzLEFT(0) - xyz(0); //dn_y/dz_{i,j-1}
    normalDerivative(2) = 0; //dnz_dz_{i,j-1}
  }

  // Ara's Original
  /*  if (flag == 0){ //wrt z_{i,j}
      normalDerivative(0) = -xyzLEFT(1) + xyzTOP(1); //dn_x/dz_{ij}
      normalDerivative(1) = -xyzTOP(0) + xyzLEFT(0); //dn_y/dz_{ij}
      normalDerivative(2) = 0; //dnz_dz_{ij}
      }
      if (flag == 1){ //wrt z_{i-1,j} //LEFT
      normalDerivative(0) = -xyzTOP(1) + xyz(1); //dn_x/dz_{i-1, j}
      normalDerivative(1) = -xyz(0) + xyzTOP(0); //dn_y/dz_{i-1,j}
      normalDerivative(2) = 0; //dnz_dz_{i-1,j}
      }
      if (flag == 2){ //wrt z_{i,j-1}
      normalDerivative(0) = -xyz(1) + xyzLEFT(1); //dn_x/dz_{i, j-1}
      normalDerivative(1) = -xyzLEFT(0) + xyz(0); //dn_y/dz_{i,j-1}
      normalDerivative(2) = 0; //dnz_dz_{i,j-1}
      }
   */
  return normalDerivative;
}

//computes the cosine derivative wrt to the height map of the angle between two vectors stored in variables normal and direction
//direction vector is normalized 
float ComputeCosDerivative(Vector3 normal, Vector3 direction, Vector3 normalDerivative)
{
  float normalNorm = normal(0)*normal(0) + normal(1)*normal(1) + normal(2)*normal(2);
  float denominator = normalNorm*sqrt(normalNorm);
  float nominator = (normalDerivative(0)*direction(0)+normalDerivative(1)*direction(1))*normalNorm - (normalDerivative(0) + normalDerivative(1))*(normal(0)*direction(0) + normal(1)*direction(1) + normal(2)*direction(2));
  float cosEDeriv = nominator/denominator;
  return cosEDeriv; 
}

float ComputeReliefDerivative(Vector3 xyz,Vector3 xyzLEFT,Vector3 xyzTOP, Vector3 normal, ModelParams inputImgParams, int flag)
{
  float reliefDeriv;
  Vector3 sunPos = inputImgParams.sunPosition;
  Vector3 viewPos = inputImgParams.spacecraftPosition;

  Vector3 normalDerivative = ComputeNormalDerivative(flag, xyz, xyzTOP, xyzLEFT);

  //compute /mu_0 = cosine of the angle between the light direction and the surface normal.
  // sun coordinates relative to the xyz point on the Moon surface
  Vector3 sunDirection = normalize(sunPos-xyz);
  float mu_0 = dot_prod(sunDirection,normal);

  //compute  /mu = cosine of the angle between the viewer direction and the surface normal.
  // viewer coordinates relative to the xyz point on the Moon surface
  Vector3 viewDirection = normalize(viewPos-xyz);
  float mu = dot_prod(viewDirection,normal);

  // Taemin's modification
  float cosEDeriv = ComputeCosDerivative(normal, viewDirection, normalDerivative); 
  float cosIDeriv = ComputeCosDerivative(normal, sunDirection, normalDerivative); 

  // Ara's Original
  //  float cosEDeriv = ComputeCosDerivative(normal, viewPos, normalDerivative); 
  //  float cosIDeriv = ComputeCosDerivative(normal, sunPos, normalDerivative); 


  //Alfred McEwen's model
  float A = -0.019*180/3.141592;
  float B =  0.000242*180/3.141592*180/3.141592;//0.242*1e-3;
  float C = -0.00000146*180/3.141592*180/3.141592*180/3.141592;//-1.46*1e-6;

  float cos_alpha = dot_prod(sunDirection,viewDirection);

  if ((cos_alpha > 1)||(cos_alpha< -1)){
    printf("cos_alpha error\n");
  }

  float rad_alpha = acos(cos_alpha);	
  float L = 1.0 + A*rad_alpha + B*rad_alpha*rad_alpha + C*rad_alpha*rad_alpha*rad_alpha;

  reliefDeriv = (mu+mu_0) ? (1-L)*cosIDeriv + L*2*(cosIDeriv*(mu+mu_0)+(cosEDeriv+cosIDeriv)*mu)/((mu+mu_0)*(mu+mu_0)) : 0;
  /*
     if (mu_0<0 || mu<0) {
     std::cout << " sun direction " << sunDirection << " view direction " << viewDirection << " normal " << normal << std::endl;
     std::cout << " cos_alpha " << cos_alpha << " incident " << mu_0 << " emission " << mu << " L = " << L << " reliefDeriv = " << reliefDeriv << std::endl;
     } else {

     }
   */
  return reliefDeriv;
}

// Josh moved to ReconstructError.cc and renamed to ComputeError
//float ComputeReconstructError(float intensity, float T, float albedo,
//                              float reflectance) {
//  float error;
//  error = (intensity-T*albedo*reflectance); /*+ (xyz_prior[2]-xyz_prior[2]);*/
//	std::cout << "intensity " << intensity << " albedo " << albedo << " reflectance " << reflectance << std::endl;
//  return error;
//}


//call function for the update of the height map. main call function for shape from shading - from multiple images
void
vw::photometry::UpdateHeightMap(ModelParams inputImgParams, std::vector<ModelParams> overlapImgParams, GlobalParams globalParams) { 
  std::string inputImgFilename = inputImgParams.inputFilename;//the original DRG
  std::string shadowFilename = inputImgParams.shadowFilename; //shadow map
  std::string outputImgFilename = inputImgParams.outputFilename; //albedo map
  std::string meanDEMFilename = inputImgParams.meanDEMFilename; //original mean DEM

  //josh
  std::string sfsDEMFilename = inputImgParams.sfsDEMFilename;
  std::string errorHeightFilename = inputImgParams.errorHeightFilename; //this error file will be in terms of height - may want to make a separate file for this at some point - Josh

  DiskImageView<PixelGray<float> >  meanDEM(meanDEMFilename);
  GeoReference DEM_geo;
  read_georeference(DEM_geo, meanDEMFilename);

  ImageView<PixelGray<float> >  sfsDEM(meanDEM.cols(), meanDEM.rows());
  sfsDEM = copy(meanDEM);


  DiskImageView<PixelMask<PixelGray<uint8> > >  shadowImage(shadowFilename);
  DiskImageView<PixelMask<PixelGray<uint8> > >  inputImage(inputImgFilename);
  DiskImageView<PixelMask<PixelGray<uint8> > >  outputImage(outputImgFilename);
  GeoReference inputImg_geo;
  read_georeference(inputImg_geo, inputImgFilename);

  //josh
  //print out the input image filename and each of the successive overlapping files to be used
  std::cout << "input file: " << inputImgFilename << endl;
  for (int m = 0; m < (int)overlapImgParams.size(); m++){
    std::cout << "next overlap filename: " << overlapImgParams[m].inputFilename << endl;
  }
  //std::cin.get();

  //josh
  ImageView<PixelMask<PixelGray<float> > > errorImage(inputImage.cols(), inputImage.rows());
  // initialize error height image
  for (int k = 0; k < inputImage.rows(); ++k){
    for (int l = 0; l < inputImage.cols(); ++l){
      errorImage(l,k) = 0;
    }
  }

  DiskImageView<PixelGray<float> > interp_dem_image(meanDEMFilename);
  // ara's original
  //ImageViewRef<PixelGray<float> >  interp_dem_image = interpolate(edge_extend(meanDEM.impl(), ConstantEdgeExtension()), BilinearInterpolation());


  //this does not match the way we input data in the code (l_index x l_index)
  Vector<float, BlockArea> lhs;
  Matrix<float, BlockArea, BlockArea> rhs;
  vector<Matrix<float, AugmentedBlockArea, BlockArea> >finalJacobianArray;
  vector<Vector<float, AugmentedBlockArea> > finalErrorVectorArray;
  vector<Matrix<float, ExtendedArea, ExtendedArea> >jacobianArray;
  vector<Vector<float, ExtendedArea> > errorVectorArray;

  jacobianArray.resize((int)overlapImgParams.size()+1);
  errorVectorArray.resize((int)overlapImgParams.size()+1);
  finalJacobianArray.resize((int)overlapImgParams.size()+1);
  finalErrorVectorArray.resize((int)overlapImgParams.size()+1);

  // array for checking valid blocks
  vector<Vector<float, 1> > validBlocks;
  jacobianArray.resize((int)overlapImgParams.size()+1);

  float recDer, recDerLEFT, recDerTOP, recErr;

  int numHorBlocks = meanDEM.cols()/horBlockSize + 1;
  int numVerBlocks = meanDEM.rows()/verBlockSize + 1;
  printf("numVerBlocks = %d, numHorBlocks = %d\n", numVerBlocks, numHorBlocks);

  // text file for storing heights
  fstream f;

  std::ostringstream sout;
  sout << "height" << UNIQUE << ".txt";
  std::string heightFilename = sout.str(); 
  //	const char*p = heightFilename.c_str();
  f.open(heightFilename.c_str(), ios::out);

  vector<Vector3> normalArray;
  vector<Vector3> xyzArray;
  vector<Vector3> xyzTOPArray;
  vector<Vector3> xyzLEFTArray;

  // reliefArray is not necessary - use a float instead
  //        vector<float> reliefArray;
  float reliefVal;

  normalArray.resize(ExtendedArea);
  xyzArray.resize(ExtendedArea);
  xyzTOPArray.resize(ExtendedArea);
  xyzLEFTArray.resize(ExtendedArea);
  //        reliefArray.resize(ExtendedArea);

  int l, k, ii, jj;

  for (int kb = 0; kb < numVerBlocks; ++kb) {
    for (int lb = 0; lb < numHorBlocks; ++lb) {

      // printf("kb = %d, lb=%d, numVerBlocks = %d, numHorBlocks = %d\n", kb, lb, numVerBlocks, numHorBlocks);

      int n = 0;

      //why did taemin and i make this loop for dummy size?  isn't the image size still blockarea?
      for (k = 0; k < verExtendedSize; ++k){
        for (l = 0; l < horExtendedSize; ++l) {
          ii = kb*verExtendedSize+k; //row index for the entire image
          jj = lb*horExtendedSize+l; //col index for the entire image
          if ((ii < inputImage.rows()) && (jj < inputImage.cols())){
            if ( is_valid(inputImage(jj,ii)) ){
              n++;
            }
          }
        }
      }

      if ( n < AugmentedBlockArea ) {//why is this using augmentedblockarea and not dummyarea?
        printf("kb = %d, lb=%d is skipped, n =%d \n", kb, lb, n);
        continue;
      }

      //this looks correct!
      std::cout << "initialization of the jacobian and the error vector" << endl;
      for (int m = 0; m < overlapImgParams.size()+1;m++){
        for (k = 0; k < ExtendedArea; k++){
          errorVectorArray[m](k) = 0.0;
          for (l = 0; l < ExtendedArea; l++) {
            jacobianArray[m](k, l) = 0.0;
          }
        }
      }
      std::cout << "initialization of the final jacobian and the final error vector" << endl;
      for (int m = 0; m < overlapImgParams.size()+1;m++){
        for (k = 0; k < AugmentedBlockArea; k++){
          finalErrorVectorArray[m](k) = 0.0;
          for (l = 0; l < BlockArea; l++) {
            finalJacobianArray[m](k, l) = 0.0;
          }
        }
      }

      for (k = temp; k < verExtendedSize; ++k) {
        for (l = temp; l < horExtendedSize; ++l) {
          ii = kb*verBlockSize+k; //row index for the entire image
          jj = lb*horBlockSize+l; //col index for the entire image
          //printf("ii = %d, jj = %d, width = %d, height = %d\n", ii, jj, meanDEM.cols(), meanDEM.rows());

          if ((ii < inputImage.rows()) && (jj < inputImage.cols())){
            //local index in the vector that describes the block image; assumes row-wise concatenation.
            int l_index = k*horExtendedSize+l; 

            std::cout << "josh & taemin changes" << endl;
            if ( is_valid(inputImage(jj,ii)) ) {
              //std::cout << "ii: " << ii << "  jj: " << jj << "  l_index: " << l_index << "  k "<<  k << "  l " << l << endl;
              //std::cout << "rows: " << inputImage.rows() << "  cols: " << inputImage.cols() << endl;
              //std::cin.get();
              Vector2 input_img_pix (jj,ii);
              Vector2 lon_lat = inputImg_geo.pixel_to_lonlat(input_img_pix);
              Vector2 input_dem_pix = DEM_geo.lonlat_to_pixel(inputImg_geo.pixel_to_lonlat(input_img_pix));
              //std::cout << "prob1";
              //int x = jj;
              //int y = ii;

              // ara's original
              int x = (int)input_dem_pix[0];
              int y = (int)input_dem_pix[1];
              //std::cout << "prob2";

              Vector3 lonlat3(lon_lat(0),lon_lat(1),(interp_dem_image)(x, y));
              xyzArray[l_index] = inputImg_geo.datum().geodetic_to_cartesian(lonlat3);//3D coordinates in the img coordinates
              //std::cout << "prob3";

              Vector2 input_img_left_pix;
              input_img_left_pix(0) = jj-1;
              input_img_left_pix(1) = ii;
              //std::cout << "prob4";

              Vector2 input_img_top_pix;
              input_img_top_pix(0) = jj;
              input_img_top_pix(1) = ii-1;
              //std::cout << "prob5";

              //check for valid DEM pixel value and valid left and top coordinates
              if ((input_img_left_pix(0) >= 0) && (input_img_top_pix(1) >= 0) && (interp_dem_image(x,y) != -10000)){
                //std::cout << "prob6";

                //determine the 3D coordinates of the pixel left of the current pixel
                Vector2 input_dem_left_pix = DEM_geo.lonlat_to_pixel(inputImg_geo.pixel_to_lonlat(input_img_left_pix));
                Vector2 lon_lat_left = inputImg_geo.pixel_to_lonlat(input_img_left_pix);
                Vector3 longlat3_left(lon_lat_left(0),lon_lat_left(1),(interp_dem_image)(input_dem_left_pix(0), input_dem_left_pix(1)));
                Vector3 xyz_left = inputImg_geo.datum().geodetic_to_cartesian(longlat3_left);
                xyzLEFTArray[l_index] = xyz_left;

                //determine the 3D coordinates of the pixel top of the current pixel
                Vector2 input_dem_top_pix = DEM_geo.lonlat_to_pixel(inputImg_geo.pixel_to_lonlat(input_img_top_pix));
                Vector2 lon_lat_top = inputImg_geo.pixel_to_lonlat(input_img_top_pix);
                Vector3 longlat3_top(lon_lat_top(0),lon_lat_top(1),(interp_dem_image)(input_dem_top_pix(0), input_dem_top_pix(1)));
                Vector3 xyz_top = inputImg_geo.datum().geodetic_to_cartesian(longlat3_top);
                xyzTOPArray[l_index] = xyz_top;

                //Vector3 normal = computeNormalFrom3DPoints(xyz, xyz_left, xyz_top);
                // Taemin's modification
                normalArray[l_index] = cross_prod(xyz_top-xyzArray[l_index], xyz_left-xyzArray[l_index]);
                // std::cout << "l index: " << l_index << " xyz_top: " << xyz_top << "xyz_left: " << xyz_left << "xyzArray[l_index] " << xyzArray[l_index] << "normalArray: " << normalArray[l_index] << std::endl;

                // moved to stage 2 and stage 3	since reliefArray should be different depending on the image
                // reliefArray[l_index] = ComputeReflectance(normalize(normalArray[l_index]), xyzArray[l_index], inputImgParams, globalParams);
              }
            }
          }
        } //for l
      } //for k 

      printf ("done stage 1\n");

      //compute the jacobian and the error vector for the inputImage 
      for (k = temp; k < verExtendedSize; ++k) { 
        for (l = temp; l < horExtendedSize; ++l) { 
          //std::cout << "here1";
          ii = kb*verBlockSize+k; //row index for the entire image
          jj = lb*horBlockSize+l; //col index for the entire image

          if ((ii < inputImage.rows()) && (jj < inputImage.cols())){

            //local index in the vector that describes the block image; assumes row-wise concatenation.
            int l_index = k*horExtendedSize+l; 

            //this is the small DRG and the large DEM. needs a fix.
            Vector2 input_img_pix(jj,ii);

            //printf("jj = %d, ii = %d\n", jj, ii);

            //update from the main image
            if (is_valid(inputImage(jj,ii)) && (shadowImage(jj, ii) == 0)){
              //std::cout << "here2.3";

              float weight;
              if (globalParams.useWeights == 1){
                weight = ComputeLineWeights(input_img_pix, inputImgParams.centerLine, inputImgParams.maxDistArray);
                //printf("-");
              }	else{
                weight = 1.0;
                //printf(".");
              }
              //printf("xyz(m) = %f %f %f\n", xyzArray[l_index][0], xyzArray[l_index][1], xyzArray[l_index][2]);
              //printf("xyzLEFT(m) = %f %f %f\n", xyzLEFTArray[l_index][0], xyzLEFTArray[l_index][1], xyzLEFTArray[l_index][2]);
              //printf("xyzTOP(m) = %f %f %f\n", xyzTOPArray[l_index][0], xyzTOPArray[l_index][1], xyzTOPArray[l_index][2]);
              //printf("normal(m) = %f %f %f\n", normalArray[l_index][0], normalArray[l_index][1], normalArray[l_index][2]);
              //std::cout << "here3";

              //current
              recDer = ComputeReliefDerivative(xyzArray[l_index], xyzLEFTArray[l_index],xyzTOPArray[l_index], normalArray[l_index], 
                  inputImgParams, 0)*(float)outputImage(jj,ii)*inputImgParams.exposureTime;

              jacobianArray[0](l_index, l_index) = recDer*weight;

              // ara changes
              //if (l_index > 0){
              //top
              if(l_index >= horExtendedSize){
                recDerTOP = ComputeReliefDerivative(xyzArray[l_index], xyzLEFTArray[l_index],
                    xyzTOPArray[l_index], normalArray[l_index], inputImgParams, 2)*(float)outputImage(jj,ii)*inputImgParams.exposureTime;

                jacobianArray[0](l_index, l_index-horExtendedSize) = recDerTOP*weight;
              }
              //if (l_index > horExtendedSize-1){
              //left
              //josh changed to not compute left when we are at the first column
              if(l_index % horExtendedSize != 0){
                recDerLEFT = ComputeReliefDerivative(xyzArray[l_index], xyzLEFTArray[l_index],
                    xyzTOPArray[l_index], normalArray[l_index], 
                    inputImgParams, 1)*(float)outputImage(jj,ii)*inputImgParams.exposureTime;
                jacobianArray[0](l_index, l_index-1) = recDerLEFT*weight;
              }
              //reliefArray[l_index] = ComputeReflectance(normalize(normalArray[l_index]), xyzArray[l_index], inputImgParams, globalParams);
              reliefVal = ComputeReflectance(normalize(normalArray[l_index]), xyzArray[l_index], inputImgParams, globalParams);

              //recErr = ComputeError((float)inputImage(jj, ii), inputImgParams.exposureTime, (float)outputImage(jj, ii), reliefArray[l_index]);
              recErr = ComputeError((float)inputImage(jj, ii), inputImgParams.exposureTime, (float)outputImage(jj, ii), reliefVal);

              //std::cout << "intensity error: " << recErr;
              //std::cin.get();

              //check relief and weight vals
              //std::cout << "     relief: " << reliefVal << "     weight" << weight << endl;
              //std::cin.get();

              errorVectorArray[0](l_index) = recErr*weight;
              //printf("recDer3 = %f %f %f recErr = %f, expT = %f\n",  recDer, recDerTOP, recDerLEFT, recErr, inputImgParams.exposureTime); 
            }//if !shadowImage
          }

        //std::cout << "here5";

        }//l
      }//k

      printf("done stage 2\n");
 
      //std::cout << "Jacob2: " << jacobianArray[0] << "\n";
 
      //compute the jacobian and the error vector for the remaining image
      //#if 0
//      std::cout << "overlapims " << overlapImgParams.size() << endl; 
//      for (int m = 0; m < 1 /*(int)overlapImgParams.size()*/; m++){ //debug by just including one overlapping image
//        DiskImageView<PixelMask<PixelGray<uint8> > >  overlapImg(overlapImgParams[m].inputFilename);
//        std::cout << "overlapfilename" << overlapImgParams[m].inputFilename << endl;
//        //std::cin.get();
//        GeoReference overlapImg_geo;
//        read_georeference(overlapImg_geo, overlapImgParams[m].inputFilename);
//        ImageViewRef<PixelMask<PixelGray<uint8> > >  interpOverlapImg = interpolate(edge_extend(overlapImg.impl(),ConstantEdgeExtension()),BilinearInterpolation());
// 
//        DiskImageView<PixelMask<PixelGray<uint8> > >  overlapShadowImage(overlapImgParams[m].shadowFilename);
// 
//        //this could cause issues if interpolated mask gives us bad results - what is an interpolated mask?
//        ImageViewRef<PixelMask<PixelGray<uint8> > >  interpOverlapShadowImage = interpolate(edge_extend(overlapShadowImage.impl(),
//              ConstantEdgeExtension()),
//            BilinearInterpolation());
//        int nn = 0;
// 
//        for (k = temp; k < verExtendedSize; ++k) {
//          for (l = temp; l < horExtendedSize; ++l) {
// 
//            ii = kb*verBlockSize+k; //row index for the entire image
//            jj = lb*horBlockSize+l; //col index for the entire image
// 
//            if ((ii < inputImage.rows()) && (jj < inputImage.cols())){
// 
//              //local index in the vector that describes the block image; assumes row-wise concatenation.
//              int l_index = k*horExtendedSize+l; 
//              //std::cout << "ii: " << ii << "jj: " << jj << "l_index: " << l_index << "m " << m << "k "<<  k << "l " << l << endl; 
//              //std::cin.get();
//              Vector2 input_img_pix(jj,ii);
// 
// 
//              //update from the overlapping images  
//              //printf("overlap_img = %s\n", overlapImgParams[m].inputFilename.c_str());
// 
//              // moved outside of k and l loop
//              // DiskImageView<PixelMask<PixelGray<uint8> > >  overlapImg(overlapImgParams[m].inputFilename);
//              // GeoReference overlapImg_geo;
//              // read_georeference(overlapImg_geo, overlapImgParams[m].inputFilename);
//              // ImageViewRef<PixelMask<PixelGray<uint8> > >  interpOverlapImg = interpolate(edge_extend(overlapImg.impl(),ConstantEdgeExtension()),BilinearInterpolation());
//              //			
//              // DiskImageView<PixelMask<PixelGray<uint8> > >  overlapShadowImage(overlapImgParams[m].shadowFilename);
//              // ImageViewRef<PixelMask<PixelGray<uint8> > >  interpOverlapShadowImage = interpolate(edge_extend(overlapShadowImage.impl(),
//              //   ConstantEdgeExtension()), BilinearInterpolation());
// 
//              //determine the corresponding pixel in the overlaping image
//              Vector2 overlap_pix = overlapImg_geo.lonlat_to_pixel(inputImg_geo.pixel_to_lonlat(input_img_pix));
//              int x = (int)overlap_pix[0];
//              int y = (int)overlap_pix[1];
// 
//              //compute and update matrix for non shadow pixels
//              if ((x>=0) && (x < overlapImg.cols()) && (y>=0) && (y< overlapImg.rows())){ //&& (interpOverlapShadowImage(x, y) == 0)){
//                nn++; 
//                //}
//                //if (nn == n){  
//                float weight;
//                if (globalParams.useWeights == 1){
//                  weight = ComputeLineWeights(overlap_pix, overlapImgParams[m].centerLine, overlapImgParams[m].maxDistArray);
//                  //printf("-");
//                }else{
//                  weight = 1.0;
//                  //printf(".");
//                }  
//                // added to account for different reflectance of different images
// 
//                //current
//                recDer = ComputeReliefDerivative(xyzArray[l_index], xyzLEFTArray[l_index],
//                    xyzTOPArray[l_index], normalArray[l_index], 
//                    overlapImgParams[m], 0)*(float)outputImage(jj,ii)*overlapImgParams[m].exposureTime;
//                jacobianArray[m+1](l_index, l_index) = recDer*weight;
// 
//                if (l_index >= horExtendedSize){
//                  //top
//                  recDerTOP = ComputeReliefDerivative(xyzArray[l_index], xyzLEFTArray[l_index],
//                      xyzTOPArray[l_index], normalArray[l_index], 
//                      overlapImgParams[m], 2)*(float)outputImage(jj,ii)*overlapImgParams[m].exposureTime;
//                  jacobianArray[m+1](l_index, l_index-horExtendedSize) = recDerTOP*weight;
//                }
//                if (l_index % horExtendedSize != 0){
//                  //left
//                  recDerLEFT = ComputeReliefDerivative(xyzArray[l_index],  xyzLEFTArray[l_index],
//                      xyzTOPArray[l_index], normalArray[l_index], 
//                      overlapImgParams[m], 1)*(float)outputImage(jj,ii)*overlapImgParams[m].exposureTime;
//                  jacobianArray[m+1](l_index, l_index-1) = recDerLEFT*weight; 
//                }
//
//                //reliefArray[l_index] = ComputeReflectance(normalize(normalArray[l_index]), xyzArray[l_index], overlapImgParams[m], globalParams);
//                reliefVal = ComputeReflectance(normalize(normalArray[l_index]), xyzArray[l_index], overlapImgParams[m], globalParams);
//
//                //recErr = ComputeError((float)interpOverlapImg(x, y), overlapImgParams[m].exposureTime, (float)outputImage(jj, ii), reliefArray[l_index]);
//                recErr = ComputeError((float)interpOverlapImg(x, y), overlapImgParams[m].exposureTime, (float)outputImage(jj, ii), reliefVal);
//                errorVectorArray[m+1](l_index) = recErr*weight;
//              }
//            } 
//            //printf("recDer3 = %f %f %f recErr = %f, expT = %f\n",  recDer, recDerTOP, recDerLEFT, recErr, inputImgParams.exposureTime);
//          }//l
//        }//k
//      
//        //print jacobian and error after this to see if they are zero
//        //std::cout << "Jacob: " << jacobianArray[m+1] << endl;
//        //std::cout << "Errorvec: " << errorVectorArray[m+1] << endl;
//
//        //if (n != nn){
//        //std::cout << "n = " << n << "     nn = " << nn << "    kb = " << kb << "     lb = " << lb << "     overlapfile = " << overlapImgParams[m].inputFilename << "    m = " << m << endl;
//        //std::cin.get();
//        //}
//      }//m
//      printf("done stage 3\n");

      //if (n != nn)
      //continue

      //#endif

      //print the error vector

      //printf("init lhs and rhs\n"); 

      //initialization of the lhs and rhs - this looks correct!
      for (int k = 0; k < BlockArea; k++) {
        lhs(k) = 0.0; //0.0
        for (int l = 0; l < BlockArea; l++){
          rhs(k, l) = 0.0; 
        } 
      }

      std::cout << "compute lhs & rhs\n" << endl; 
      //compute lhs and rhs
      for (int m = 0; m < (int)overlapImgParams.size()+1; m++){

        std::cout << "start jacobian copy" << endl;
        // copy jacobianArray to finalJacobianArray

        for (k = temp; k < verBlockSize; ++k) {
          for (l = temp; l < horBlockSize; ++l) {

            //local index in the vector that describes the block image; assumes row-wise concatenation.
            int l_index = k*horExtendedSize+l;
            int ll_index = k*horBlockSize+l; // realindex to be stored
            //std::cout << "l_index: " << l_index << " ll_index: " << ll_index << " m " << m << " k "<<  k << " l " << l << endl; 
            //	std::cin.get();
            finalJacobianArray[m](ll_index, ll_index) = jacobianArray[m](l_index, l_index); //current
            if (l_index > 0) finalJacobianArray[m](ll_index, ll_index-1) = jacobianArray[m](l_index, l_index-1); //left
            if (l_index >= horBlockSize) finalJacobianArray[m](ll_index-horBlockSize, ll_index) = jacobianArray[m](l_index-horExtendedSize, l_index); //top
            finalErrorVectorArray[m](ll_index) = errorVectorArray[m](l_index);
          } // l
        } // k 
        rhs = rhs + transpose(finalJacobianArray[m])*finalJacobianArray[m];
        lhs = lhs + transpose(finalJacobianArray[m])*finalErrorVectorArray[m];  // taemin added transpose 
        // when we only have 1 image, this should be zero
      } // m 

      //print the jacobian 
      for (int row = 0; row< BlockArea; row++){
        for (int col = 0; col < BlockArea; col++){ 
          f << finalJacobianArray[0](row, col) << ","; 
        }
        f << endl;
      }

      //solves lhs = rhs*x and stores results in lhs
      try { 
        solve_symmetric_nocopy(rhs,lhs); 
        for (k = temp ; k < verBlockSize; ++k) {
          for (l = temp; l < horBlockSize; ++l) { 
  
            ii = kb*verBlockSize+k; //row index for the entire image
            jj = lb*horBlockSize+l; //col index for the entire image
  
            if ((ii < inputImage.rows()) && (jj < inputImage.cols())){
              //local index in the vector that describes the block image; assumes row-wise concatenation.
              int l_index = k*horBlockSize+l; 
  
              //std::cout << "changes start here";
  
              //test(jj,ii) = meanDEM(jj,ii) + lhs(l_index;
              if ((meanDEM(jj,ii).v() == -10000)){
                errorImage(jj,ii) = 0; //-10000
              }
              else{
                std::cout << "oldDEM: " << meanDEM(jj,ii).v();
                sfsDEM(jj, ii) = sfsDEM(jj, ii) + lhs(l_index);
                errorImage(jj, ii) = lhs(l_index);
                //if (writefiles == 1){
                //  f << lhs(l_index) << endl; // text file with height errors
                //}
                //if (lhs(l_index) > 1000){
                //std::cout << "RHS: " << rhs << "\n";
                //std::cout << "LHS: " << lhs << "\n\n";
                std::cout << "           lhs: " << lhs(l_index) << "       errorimage: " << errorImage(jj,ii) << "          sfsDEM: " << sfsDEM(jj,ii).v() << endl; 
                std::cout << "kb = " << kb << "      lb = " << lb << endl;
                //std::cin.get();
                //}
              }
            }
          }
        }
        printf("Go\n");
      } catch (ArgumentErr &/*e*/) {
  
        std::cout << "Error @ (kb,lb) = (" << kb << "," << lb << ")\n";
        //std::cout << "Exception caught: " << ArgumentErr.what() << "\n";
        //std::cout << "PRERHS: " << pre_rhs << "\n";
        //std::cout << "PRELHS: " << pre_lhs << "\n\n";
        //std::cout << "RHS: " << rhs << "\n";
        //std::cout << "LHS: " << lhs << "\n\n";
        //std::cout << "DEBUG: " << rhs(0,1) << "   " << rhs(1,0) << "\n\n";
        printf("Error\n");
      }
  
      //solve_symmetric_nocopy(rhs, lhs);
  
      //copy lhs to back meanDEM
  
    }//lb
  }//kb

  // close text file
  f.close();

  //josh
  if (writefiles == 1){
    sfsDEMFilename.resize(sfsDEMFilename.size()-4);
    errorHeightFilename.resize(errorHeightFilename.size()-4);

    sfsDEMFilename = sfsDEMFilename + UNIQUE + ".tif";
    errorHeightFilename = errorHeightFilename + UNIQUE + ".tif";

    std::cout << "time to write the files" << endl;

    //write in the updated DEM
    write_georeferenced_image(sfsDEMFilename, sfsDEM,
        DEM_geo, TerminalProgressCallback("photometry","Processing:"));

    //josh
    write_georeferenced_image(errorHeightFilename, errorImage,
        DEM_geo, TerminalProgressCallback("photometry","Processing:"));
  }
}

#if 0
//old function to be grandfather-ed.
//call function for the update of the height map. main call function for shape from shading - from multiple images
void
vw::photometry::UpdateHeightMap(ModelParams inputImgParams, std::vector<ModelParams> overlapImgParams, GlobalParams globalParams)
{  
  std::string inputImgFilename = inputImgParams.inputFilename;//the original DRG
  std::string shadowFilename = inputImgParams.shadowFilename; //shadow map
  std::string outputImgFilename = inputImgParams.outputFilename; //albedo map
  std::string meanDEMFilename = inputImgParams.meanDEMFilename; //original mean DEM

  DiskImageView<PixelGray<float> >  meanDEM(meanDEMFilename);
  GeoReference DEM_geo;
  read_georeference(DEM_geo, meanDEMFilename);

  DiskImageView<PixelMask<PixelGray<uint8> > >  shadowImage(shadowFilename);
  DiskImageView<PixelMask<PixelGray<uint8> > >  inputImage(inputImgFilename);
  GeoReference inputImg_geo;
  read_georeference(inputImg_geo, inputImgFilename);
  DiskImageView<PixelMask<PixelGray<uint8> > >  outputImage(outputImgFilename);

  ImageViewRef<PixelGray<float> >  interp_dem_image = interpolate(edge_extend(meanDEM.impl(),																					 ConstantEdgeExtension()),
      BilinearInterpolation());

  Vector<float, BlockArea> lhs, errorVector;
  Matrix<float, BlockArea, BlockArea> rhs, jacobian;

  float recDer, recDerLEFT, recDerTOP, recErr;    

  int numHorBlocks = meanDEM.cols()/horExtendedSize + 1;
  int numVerBlocks = meanDEM.rows()/verExtendedSize + 1;
  printf("numVerBlocks = %d, numHorBlocks = %d\n", numVerBlocks, numHorBlocks);

  Vector3 *normalArray = new Vector3[numVerBlocks*numHorBlocks];
  Vector3 *xyzArray = new Vector3[numVerBlocks*numHorBlocks];
  Vector3 *xyzTOPArray = new Vector3[numVerBlocks*numHorBlocks];
  Vector3 *xyzLEFTArray = new Vector3[numVerBlocks*numHorBlocks];
  float   *reliefArray = new float[numVerBlocks*numHorBlocks];

  for (int kb = 0 ; kb < numVerBlocks; ++kb) {
    for (int lb = 0; lb < numHorBlocks; ++lb) {

      printf("kb = %d, lb=%d, numVerBlocks = %d, numHorBlocks = %d\n", kb, lb, numVerBlocks, numHorBlocks);

      int n = 0;
      for (int k = 0 ; k < verExtendedSize; ++k){ 
        for (int l = 0; l < horExtendedSize; ++l) {
          int ii = kb*verExtendedSize+k; //row index for the entire image
          int jj = lb*horExtendedSize+l; //col index for the entire image
          if ((ii < inputImage.rows()) && (jj < inputImage.cols())){
            if ( is_valid(inputImage(jj,ii)) ){ 
              n++; 
            }
          }
        }
      }
      if ( n < BlockArea ) {
        printf("kb = %d, lb=%d is skipped\n", kb, lb);
        continue;
      }		

      //initialization
      for (int ii = 0; ii < BlockArea; ii++) {
        errorVector(ii) = 0.0;
        for (int jj = 0; jj < BlockArea; jj++){
          jacobian(ii, jj) = 0.0;
        }
      }

      //initialize  output_img, numSamples and norm
      for (int k = 0 ; k < verExtendedSize; ++k) {
        for (int l = 0; l < horExtendedSize; ++l) {

          int ii = kb*verExtendedSize+k; //row index for the entire image
          int jj = lb*horExtendedSize+l; //col index for the entire image
          //printf("ii = %d, jj = %d, width = %d, height = %d\n", ii, jj, meanDEM.cols(), meanDEM.rows());

          if ((ii < inputImage.rows()) && (jj < inputImage.cols())){

            //local index in the vector that describes the block image; assumes row-wise concatenation.
            int l_index = k*horExtendedSize+l; 

            /*
               Vector2 dem_pix;
               dem_pix[0] = ii;
               dem_pix[1] = jj; 
               Vector2 input_img_pix = overlap_geo.lonlat_to_pixel(input_img_geo.pixel_to_lonlat(dem_pix));
               int x = (int)overlap_pix[0];
               int y = (int)overlap_pix[1];
             */

            if ( is_valid(inputImage(jj,ii)) ) {

              Vector2 input_img_pix (jj,ii);
              Vector2 lon_lat = inputImg_geo.pixel_to_lonlat(input_img_pix);
              Vector2 input_dem_pix = DEM_geo.lonlat_to_pixel(inputImg_geo.pixel_to_lonlat(input_img_pix));

              int x = (int)input_dem_pix[0];
              int y = (int)input_dem_pix[1];

              Vector3 lonlat3(lon_lat(0),lon_lat(1),(interp_dem_image)(x, y));
              xyzArray[l_index] = inputImg_geo.datum().geodetic_to_cartesian(lonlat3);//3D coordinates in the img coordinates

              Vector2 input_img_left_pix;
              input_img_left_pix(0) = ii-1;
              input_img_left_pix(1) = jj;

              Vector2 input_img_top_pix;
              input_img_top_pix(0) = ii;
              input_img_top_pix(1) = jj-1;

              //check for valid DEM pixel value and valid left and top coordinates
              if ((input_img_left_pix(0) >= 0) && (input_img_top_pix(1) >= 0) && (interp_dem_image(x,y) != -10000)){

                //determine the 3D coordinates of the pixel left of the current pixel
                Vector2 input_dem_left_pix = DEM_geo.lonlat_to_pixel(inputImg_geo.pixel_to_lonlat(input_img_left_pix));
                Vector2 lon_lat_left = inputImg_geo.pixel_to_lonlat(input_img_left_pix);
                Vector3 longlat3_left(lon_lat_left(0),lon_lat_left(1),(interp_dem_image)(input_dem_left_pix(0), input_dem_left_pix(1)));
                Vector3 xyz_left = inputImg_geo.datum().geodetic_to_cartesian(longlat3_left);
                xyzLEFTArray[l_index] = xyz_left;

                //determine the 3D coordinates of the pixel top of the current pixel
                Vector2 input_dem_top_pix = DEM_geo.lonlat_to_pixel(inputImg_geo.pixel_to_lonlat(input_img_top_pix));
                Vector2 lon_lat_top = inputImg_geo.pixel_to_lonlat(input_img_top_pix);
                Vector3 longlat3_top(lon_lat_top(0),lon_lat_top(1),(interp_dem_image)(input_dem_top_pix(0), input_dem_top_pix(1)));
                Vector3 xyz_top = inputImg_geo.datum().geodetic_to_cartesian(longlat3_top);
                xyzTOPArray[l_index] = xyz_top;

                //Vector3 normal = computeNormalFrom3DPoints(xyz, xyz_left, xyz_top);
                // Taemin's modification
                normalArray[l_index] = cross_prod(xyz_top-xyzArray[l_index], xyz_left-xyzArray[l_index]);
                //std::cout << "xyz_top: " << xyz_top << "xyz_left: " << xyz_left << "xyzArray[l_index] " << xyzArray[l_index] << "normalArray: " << normalArray[l_index] << std::endl;

                // Ara's Original
                //normalArray[l_index] = computeNormalFrom3DPointsGeneral(xyzArray[l_index], xyz_left, xyz_top);

                reliefArray[l_index] = ComputeReflectance(normalize(normalArray[l_index]), xyzArray[l_index], inputImgParams, globalParams);
              }
            }
          }
        } //for l
      } //for k
      printf ("done stage 1\n");

      //compute the jacobian and the error vector for the inputImage
      for (int k = 0 ; k < verExtendedSize; ++k) {
        for (int l = 0; l < horExtendedSize; ++l) {

          int ii = kb*verExtendedSize+k; //row index for the entire image
          int jj = lb*horExtendedSize+l; //col index for the entire image

          if ((ii < inputImage.rows()) && (jj < inputImage.cols())){

            //local index in the vector that describes the block image; assumes row-wise concatenation.
            int l_index = k*horExtendedSize+l; 

            //this is the small DRG and the large DEM. needs a fix.
            Vector2 input_img_pix(jj,ii);

            //printf("jj = %d, ii = %d\n", jj, ii);

            //update from the main image        
            if (is_valid(inputImage(jj,ii)) && (shadowImage(jj, ii) == 0)){

              float weight;
              if (globalParams.useWeights == 1){
                weight = ComputeLineWeights(input_img_pix, inputImgParams.centerLine, inputImgParams.maxDistArray);
                printf("-");
              }	else{
                weight = 1.0;
                printf(".");
              }
              printf("xyz(m) = %f %f %f\n", xyzArray[l_index][0], xyzArray[l_index][1], xyzArray[l_index][2]);
              printf("xyzLEFT(m) = %f %f %f\n", xyzLEFTArray[l_index][0], xyzLEFTArray[l_index][1], xyzLEFTArray[l_index][2]);
              printf("xyzTOP(m) = %f %f %f\n", xyzTOPArray[l_index][0], xyzTOPArray[l_index][1], xyzTOPArray[l_index][2]);
              printf("normal(m) = %f %f %f\n", normalArray[l_index][0], normalArray[l_index][1], normalArray[l_index][2]);

              recDer = ComputeReliefDerivative(xyzArray[l_index], xyzLEFTArray[l_index],xyzTOPArray[l_index], normalArray[l_index], 
                  inputImgParams, 0)*(float)outputImage(jj,ii)*inputImgParams.exposureTime;

              jacobian(l_index, l_index) += recDer*weight;

              if (l_index > 0){
                recDerTOP = ComputeReliefDerivative(xyzArray[l_index], xyzLEFTArray[l_index],
                    xyzTOPArray[l_index], normalArray[l_index], inputImgParams, 1)*(float)outputImage(jj,ii)*inputImgParams.exposureTime;	

                jacobian(l_index, l_index-horExtendedSize) += recDerTOP*weight;
              }
              if (l_index > horExtendedSize-1){
                recDerLEFT = ComputeReliefDerivative(xyzArray[l_index], xyzLEFTArray[l_index],
                    xyzTOPArray[l_index], normalArray[l_index], 
                    inputImgParams, 2)*(float)outputImage(jj,ii)*inputImgParams.exposureTime;	
                jacobian(l_index, l_index-1) += recDerLEFT*weight;
              }
              recErr = ComputeError((float)inputImage(jj, ii), inputImgParams.exposureTime, (float)outputImage(jj, ii), reliefArray[l_index]);

              errorVector(l_index) += recErr*weight;
              printf("recDer3 = %f %f %f recErr = %f, expT = %f\n",  recDer, recDerTOP, recDerLEFT, recErr, inputImgParams.exposureTime);						 
            }//if !shadowImage
          } 
        }//l
      }//k

      printf("done stage 2\n");

      std::cout << "Jacob2: " << jacobian << "\n";

      //compute the jacobian and the error vector for the remaining images	
      //#if 0
      for (int k = 0 ; k < verExtendedSize; ++k) {
        for (int l = 0; l < horExtendedSize; ++l) {

          int ii = kb*verExtendedSize+k; //row index for the entire image
          int jj = lb*horExtendedSize+l; //col index for the entire image

          if ((ii < inputImage.rows()) && (jj < inputImage.cols())){

            //local index in the vector that describes the block image; assumes row-wise concatenation.
            int l_index = k*horExtendedSize+l; 

            Vector2 input_img_pix(jj,ii);
            //update from the overlapping images  
            for (int m = 0; m < (int)overlapImgParams.size(); m++){

              //printf("overlap_img = %s\n", overlapImgParams[m].inputFilename.c_str());

              DiskImageView<PixelMask<PixelGray<uint8> > >  overlapImg(overlapImgParams[m].inputFilename);
              GeoReference overlapImg_geo;
              read_georeference(overlapImg_geo, overlapImgParams[m].inputFilename);
              ImageViewRef<PixelMask<PixelGray<uint8> > >  interpOverlapImg = interpolate(edge_extend(overlapImg.impl(),ConstantEdgeExtension()),BilinearInterpolation());

              DiskImageView<PixelMask<PixelGray<uint8> > >  overlapShadowImage(overlapImgParams[m].shadowFilename);
              ImageViewRef<PixelMask<PixelGray<uint8> > >  interpOverlapShadowImage = interpolate(edge_extend(overlapShadowImage.impl(),
                    ConstantEdgeExtension()),
                  BilinearInterpolation());

              //determine the corresponding pixel in the overlaping image
              Vector2 overlap_pix = overlapImg_geo.lonlat_to_pixel(inputImg_geo.pixel_to_lonlat(input_img_pix));
              int x = (int)overlap_pix[0];
              int y = (int)overlap_pix[1];

              //compute and update matrix for non shadow pixels
              if ((x>=0) && (x < overlapImg.cols()) && (y>=0) && (y< overlapImg.rows()) && (interpOverlapShadowImage(x, y) == 0)){

                //float weight = ComputeLineWeights(overlap_pix, overlapImgParams[m].centerLine, overlapImgParams[m].maxDistArray);
                float weight;
                if (globalParams.useWeights == 1){
                  weight = ComputeLineWeights(overlap_pix, overlapImgParams[m].centerLine, overlapImgParams[m].maxDistArray);
                  printf("-");
                }else{
                  weight = 1.0;
                  printf(".");
                }

                recErr = ComputeError((float)interpOverlapImg(x, y), overlapImgParams[m].exposureTime, 
                    (float)outputImage(jj, ii), reliefArray[l_index]);
                errorVector(l_index) += recErr*weight;

                recDer = ComputeReliefDerivative(xyzArray[l_index], xyzLEFTArray[l_index],
                    xyzTOPArray[l_index], normalArray[l_index], 
                    overlapImgParams[m], 0)*(float)outputImage(jj,ii)*overlapImgParams[m].exposureTime;
                jacobian(l_index, l_index) += recDer*weight;

                if (l_index > 0){
                  recDerTOP = ComputeReliefDerivative(xyzArray[l_index], xyzLEFTArray[l_index],
                      xyzTOPArray[l_index], normalArray[l_index], 
                      overlapImgParams[m], 1)*(float)outputImage(jj,ii)*overlapImgParams[m].exposureTime;
                  jacobian(l_index, l_index-horExtendedSize) += recDerTOP*weight;
                }
                if (l_index > horExtendedSize-1){
                  recDerLEFT = ComputeReliefDerivative(xyzArray[l_index],  xyzLEFTArray[l_index],
                      xyzTOPArray[l_index], normalArray[l_index], 
                      overlapImgParams[m], 2)*(float)outputImage(jj,ii)*overlapImgParams[m].exposureTime;
                  jacobian(l_index, l_index-1) += recDerLEFT*weight;
                }
              }
            }//m
          }
          //printf("recDer3 = %f %f %f recErr = %f, expT = %f\n",  recDer, 
          //	recDerTOP, recDerLEFT, recErr, inputImgParams.exposureTime);						 
        }//l
      }//k
      printf("done stage 3\n");
      std::cout << "Jacob3: " << jacobian << "\n";
      //#endif
      /*
      //print the jacobian
      for (int row = 0; row< BlockArea; row++){
      for (int col = 0; col < BlockArea; col++){
      printf("%f ", jacobian(row, col));
      }
      printf("\n");
      }
       */
      //print the error vector

      //solves lhs = rhs*x and stores results in lhs
      rhs = transpose(jacobian)*jacobian;
      lhs = jacobian*errorVector;
      try {
        solve_symmetric_nocopy(rhs,lhs);
        for (int k = 0 ; k < verExtendedSize; ++k) {
          for (int l = 0; l < horExtendedSize; ++l) {

            int ii = kb*verExtendedSize+k; //row index for the entire image
            int jj = lb*horExtendedSize+l; //col index for the entire image

            if ((ii < inputImage.rows()) && (jj < inputImage.cols())){
              //local index in the vector that describes the block image; assumes row-wise concatenation.
              int l_index = k*horExtendedSize+l; 
              meanDEM(jj, ii) = meanDEM(jj, ii) + lhs(l_index);
            }
          }
        }
        printf("Go\n");
      } catch (ArgumentErr &/*e*/) {

        std::cout << "Error @ (kb,lb) = (" << kb << "," << lb << ")\n";
        //				std::cout << "Exception caught: " << ArgumentErr.what() << "\n";
        //										 std::cout << "PRERHS: " << pre_rhs << "\n";
        //										 std::cout << "PRELHS: " << pre_lhs << "\n\n";
        std::cout << "RHS: " << rhs << "\n";
        std::cout << "LHS: " << lhs << "\n\n";
        //				std::cout << "DEBUG: " << rhs(0,1) << "   " << rhs(1,0) << "\n\n";
        printf("Error\n");
      }

      //solve_symmetric_nocopy(rhs, lhs);

      //copy lhs to back meanDEM

    }//lb
  }//kb


  delete normalArray; 

  delete xyzArray;

  delete xyzLEFTArray;

  delete xyzTOPArray;

  delete reliefArray; 

  //write in the updated DEM
  write_georeferenced_image(meanDEMFilename, meanDEM,
      DEM_geo, TerminalProgressCallback("photometry","Processing:"));

}
#endif
//=================================================================================
//below is Jon's code 

// Normalizes an image such the visible range is within n_sigma standard deviations
ImageView<PixelGray<double> > softNormalize(ImageView<PixelGray<double> > image, double n_sigma)
{
  double mean, sigma, count;
  mean = 0;
  count = 0;
  sigma = 0;
  for(int x = 0; x < image.cols(); x++){
    for(int y = 0; y < image.rows(); y++){
      double pix = image(x,y);
      if( !isnan(pix) && !isinf(pix) ){
        mean += pix;
        count++;
      }
    }
  }

  mean = mean / count;
  for(int x = 0; x < (int)image.cols(); x++){
    for(int y = 0; y < (int)image.rows(); y++){
      double pix = image(x,y);
      if( !isnan(pix) && !isinf(pix) ){
        sigma += square(pix-mean);
      }
    }
  }
  sigma = sqrt(sigma / count)/2;

  double range[] = {mean - n_sigma*sigma, mean + n_sigma*sigma};
  ImageView<PixelGray<double> > image_normalize = copy(image);

  for(int x = 0; x < image.cols(); x++){
    for(int y = 0; y < image.rows(); y++){
      double pix = image(x,y);
      image_normalize(x,y) = min(1., max(0., (pix - range[0]) / (range[1] - range[0])));
    }
  }

  return image_normalize;
}

// Converts a grayscale image into a high-contrast HSV image for easy visualization
ImageView<PixelRGB<float32> > gray2hsv(ImageView<PixelGray<double> > image, double repetitions){

  double min_val, max_val;
  min_val = NAN;
  max_val = NAN;
  for(int x = 0; x < image.cols(); x++){
    for(int y = 0; y < image.rows(); y++){
      double pix = image(x,y);
      if( !isnan(pix) && !isinf(pix) ){
        if(isnan(min_val) || (pix < min_val) ){
          min_val = pix;
        }
        if(isnan(max_val) || (pix > max_val) ){
          max_val = pix;
        }
      }
    }
  }

  double pix;
  ImageView<PixelRGB<float32> > image_hsv(image.cols(), image.rows());
  for(int x = 0; x < image.cols(); x++){
    for(int y = 0; y < image.rows(); y++){
      pix = image(x,y);
      pix = (pix - min_val) / (max_val - min_val);
      pix = pix * repetitions;
      while(pix > 1){
        pix = pix - 1.0;
      }
      if( !isnan(pix) && !isinf(pix) ){
        image_hsv(x,y) = pixel_cast<PixelRGB<float32> >(PixelHSV<double>(pix, 1., 1.));
      }
    }
  }

  return image_hsv;
}


// Loads a binary image into a pre-existing image
void readBinaryImage(ImageView<PixelGray<double> > image, char* filename){
  int width = image.cols();
  int height = image.rows();
  unsigned int numel = width * height;

  FILE* file = fopen(filename,"rb");
  double data;
  unsigned int x, y;
  for(unsigned int i = 0; i < numel; i++){
    fread(&data,sizeof(double),1,file);
    y = i % height;
    x = ( i - y )/height;
    image(x,y) = data;
  }
  fclose(file);

}

// The loss function that we optimize over. Assumes that only the DEM is a free parameter, and returns the gradient of the loss against the DEM.
void lossfun_accuracy(double * loss, ImageView<PixelGray<double> > *d_loss_dem, ImageView<PixelGray<double> > *image_predicted,  ImageView<PixelGray<double> > * image, ImageView<PixelGray<double> > * dem, ImageView<PixelGray<double> > * init_dem, ImageView<PixelGray<double> > * albedo, Vector<double, 3> * light_direction){

  Vector<double, 3> normal1, normal2;
  double h, ih, hx, hy, hxy, ldotn1, ldotn2, /*ldotn,*/ im, pred, L1, L2, L3, d1, dx1, dy1, dd1, dx2, dy2, dxy2, dd2, mult_sub, mult1, mult2, a, diff, diff_sq, sigma_sq;

  (*loss) = 0.; // compute a running tally of the loss
  (*d_loss_dem) = 0*(*d_loss_dem); // Clear the gradient

  for(int x = 0; x < (*image).cols(); x++){
    for(int y = 0; y < (*image).rows(); y++){

      h = (*dem)(x,y);
      ih = (*init_dem)(x,y);

      // Compute the loss against the initial DEM, according to the defined "volume" loss function
      if( !isnan(h) && !isnan(ih) ){
        if(LOSS_VOLUME_TYPE == GAUSSIAN){
          (*loss) = (*loss) + LOSS_VOLUME_MULT*square(h - ih);
          (*d_loss_dem)(x,y) = (*d_loss_dem)(x,y) + LOSS_VOLUME_MULT*(h - ih);

        }else if(LOSS_VOLUME_TYPE == CAUCHY){

          diff = h - ih;
          diff_sq = diff*diff;
          sigma_sq = LOSS_VOLUME_SIGMA*LOSS_VOLUME_SIGMA;
          (*loss) = (*loss) + LOSS_VOLUME_MULT*log(1 + diff_sq/sigma_sq);
          (*d_loss_dem)(x,y) = (*d_loss_dem)(x,y) + (LOSS_VOLUME_MULT*2*diff) / (diff_sq + sigma_sq);

        }else if(LOSS_VOLUME_TYPE == EXPONENTIAL){

          diff = h - ih;
          (*loss) = (*loss) + LOSS_VOLUME_MULT*abs(diff);
          (*d_loss_dem)(x,y) = (*d_loss_dem)(x,y) + LOSS_VOLUME_MULT*sign(diff);

        }
      }

      a = (*albedo)(x,y);
      im = (*image)(x,y);
      hx = (*dem)(x+1,y);
      hy = (*dem)(x,y+1);
      hxy = (*dem)(x+1,y+1);
      if(isnan(im) || isnan(a) || isnan(h) || isnan(hx) || isnan(hy) || isnan(hxy)){
        (*image_predicted)(x,y) = NAN;
        continue;
      }

      // Compute the normals of the two triangles that define the surface of the current pixel.
      normal1.x() = h - hx;   normal1.y() = h - hy;   normal1.z() = 1;
      normal2.x() = hy - hxy; normal2.y() = hx - hxy; normal2.z() = 1;

      // Normalize the normals
      normal1 = normalize(normal1);
      normal2 = normalize(normal2);

      // Compute the intensity of both triangles
      ldotn1 = dot_prod(normal1, (*light_direction));
      ldotn2 = dot_prod(normal2, (*light_direction));

      // Combine the two, and the albedo, to render the whole pixel
      pred = a * (ldotn1 + ldotn2)/2;

      // If both are in shadow, the loss and the gradient are zero
      if ( (im <= 0) && (pred <= 0) ){
        continue;
      }

      // Store the rendered image intensity
      (*image_predicted)(x,y) = pred;

      // Compute the loss
      (*loss) = (*loss) + LOSS_ACCURACY_MULT * square(im - pred);


      // Compute the gradient of the loss, against the height map

      // Precache a ton of math.
      L1 = (*light_direction)[0];
      L2 = (*light_direction)[1];
      L3 = (*light_direction)[2];

      // Precache numbers for the first triangle
      dd1 = 1 + 2*square(h) + square(hx) + square(hy) - 2*h*(hx + hy);
      dd1 = sqrt(dd1);
      dd1 = dd1*(dd1*dd1);

      d1 = (1 + h*hx - (h + hx)*hy + square(hy))*L1 + L2 + (-h + hx)*(hx - hy)*L2 + (-2*h + hx + hy)*L3;
      dx1 = -((1 + square(h - hy))*L1) + (h - hx)*(h*L2 - hy*L2 + L3);
      dy1 = -L2 + (h - hx)*(-(hy * L1) + h*(L1 - L2) + hx*L2) + (h - hy)*L3;

      // Precache numbers for the second triangle
      dd2 = 1 + square(hx) - 2*hx*hxy + 2*square(hxy) - 2*hxy*hy + square(hy);
      dd2 = sqrt(dd2);
      dd2 = dd2*(dd2*dd2);

      dx2 = L2 + (hxy - hy)*(hx*L1 - hy*L2 + hxy*(-L1 + L2)) + (-hx + hxy)*L3;
      dy2 = (1 + square(hx - hxy))*L1 + (hxy - hy)*(hx * L2 - hxy*L2 + L3);
      dxy2 = -((1 + square(hx) + hxy*hy - hx*(hxy + hy))*L1) - L2 + (hx - hy)*(-hxy + hy)*L2 + (hx - 2*hxy + hy)*L3;

      mult_sub = LOSS_ACCURACY_MULT * (pred - im) * a;
      mult1 = mult_sub / dd1;
      mult2 = mult_sub / dd2;

      // Push the gradient onto the four corners of the pixel.
      (*d_loss_dem)(x,y)     = (*d_loss_dem)(x,y)     + mult1 * d1;
      (*d_loss_dem)(x+1,y)   = (*d_loss_dem)(x+1,y)   + mult1 * dx1   + mult2 * dx2;
      (*d_loss_dem)(x,y+1)   = (*d_loss_dem)(x,y+1)   + mult1 * dy1   + mult2 * dy2;
      (*d_loss_dem)(x+1,y+1) = (*d_loss_dem)(x+1,y+1) + mult2 * dxy2;

    }
  }
}

// Simple gradient descent, will basically always be worse than conjugate gradient descent.
void optimize_simple(ImageView<PixelGray<double> > *image_predicted,  ImageView<PixelGray<double> > * image, ImageView<PixelGray<double> > * dem, ImageView<PixelGray<double> > * init_dem, ImageView<PixelGray<double> > * albedo, Vector<double, 3> * light_direction){

  ImageView<PixelGray<double> > last_dem = copy((*init_dem));
  ImageView<PixelGray<double> > d_loss_dem((*dem).cols(), (*dem).rows());

  double loss, last_loss, step_size;
  clock_t start, end;
  double cpu_time_used;
  start = clock();

  step_size = 1;
  double f_percent = 0.001;
  int f_streak = 10;
  int streak_count = 0;
  last_loss = NAN;
  for(int iter = 1; iter < 100000; iter++){

    lossfun_accuracy(&loss, &d_loss_dem, image_predicted, image, dem, init_dem, albedo, light_direction);

    if(iter > 1){
      if(loss <= last_loss){
        step_size = step_size * 1.1;
      }else{
        (*dem) = copy(last_dem);
        step_size = step_size * 0.1;
      }

      if( abs((loss - last_loss)/last_loss) <= (f_percent/100.0)){
        streak_count++;
      }else{
        streak_count = 0;
      }
    }

    printf("%d:\tf = %f\t%f\t", iter, loss, step_size);
    for(int t = 0; t < streak_count; t++){
      printf("X");
    }
    printf("\n");

    if(streak_count >= f_streak){
      break;
    }

    last_dem = copy(*dem);
    last_loss = loss;

    (*dem) = (*dem) - (step_size * d_loss_dem);

  }

  end = clock();
  cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
  printf("Done in %f minutes\n", cpu_time_used/60);

}


// Computes the numerical gradient, and compares to the analytical one.
void optimize_check_gradient(ImageView<PixelGray<double> > *image_predicted,  ImageView<PixelGray<double> > *image, ImageView<PixelGray<double> > *dem, ImageView<PixelGray<double> > *init_dem, ImageView<PixelGray<double> > *albedo, Vector<double, 3> *light_direction){

  double eps = .00001;
  double f0, f1, bak, df_a, df_n;
  ImageView<PixelGray<double> > df0((*dem).cols(), (*dem).rows());
  ImageView<PixelGray<double> > df1((*dem).cols(), (*dem).rows());

  lossfun_accuracy(&f0, &df0, image_predicted, image, dem, init_dem, albedo, light_direction);

  for(int x = 0; x < (*image).cols(); x++){
    for(int y = 0; y < (*image).rows(); y++){
      if(!(isnan( double((*image)(x,y)) ) ||
            isnan( double((*dem)(x,y)) ) ||
            isnan( double((*albedo)(x,y)) ) ) ){
        bak = (*dem)(x,y);

        (*dem)(x,y) = (*dem)(x,y) + eps;
        lossfun_accuracy(&f1, &df1, image_predicted, image, dem, init_dem, albedo, light_direction);
        (*dem)(x,y) = bak;
        df_a = (double)df0(x,y);
        df_n = (f1 - f0)/eps;

        printf("%g / %g : %g%%\n", df_a, df_n, 100*abs((df_n - df_a) / (abs(df_a) + abs(df_n))));
      }
    }
  }
}

// Takes the sum of an image, used in conjugate gradient descent instead of a dot product.
double image_sum( ImageView<PixelGray<double> > *im ){
  double val = 0;
  for(int x = 0; x < (*im).cols(); x++){
    for(int y = 0; y < (*im).rows(); y++){
      val += (*im)(x,y);
    }
  }
  return val;
}

// Does conjugate gradient descent on the DEM, keeping all else fixed.
void
vw::photometry::optimize_conjugate_gradient(ImageView<PixelGray<double> > *image_predicted,  ImageView<PixelGray<double> > *image, ImageView<PixelGray<double> > *dem,
    ImageView<PixelGray<double> > *init_dem, ImageView<PixelGray<double> > *albedo, Vector<double, 3> *light_direction){

  int MAX_ITERS = 100000;
  double INT = 0.1;   // don't reevaluate within 0.1 of the limit of the current bracket
  double EXT = 3.0;  // extrapolate maximum 3 times the current step-size
  double RATIO = 10;  // maximum allowed slope ratio
  double MAX = 20;    // length of linesearch

  // Wolfe-powell parameters
  double SIG = 0.1;
  double RHO = SIG/2;

  bool VERBOSE = false;
  double f0;
  ImageView<PixelGray<double> > df0((*dem).cols(), (*dem).rows());

  double f_percent = 0.05;
  int f_streak = 10;
  int streak_count = 0;

  double last_loss;
  clock_t start, end;
  double cpu_time_used;
  start = clock();

  lossfun_accuracy(&f0, &df0, image_predicted, image, dem, init_dem, albedo, light_direction);

  last_loss = f0;
  printf("0:\tf = %f\n", f0);

  ImageView<PixelGray<double> > s = -copy(df0);
  ImageView<PixelGray<double> > tmp = s * s;
  double d0 = (-1.) * image_sum(&tmp);
  double x3 = 1./(1.-d0); // initial step

  int i = 0;
  ImageView<PixelGray<double> > dF0;
  ImageView<PixelGray<double> > dem0;
  ImageView<PixelGray<double> > df1;
  ImageView<PixelGray<double> > df2 = copy(df0);
  ImageView<PixelGray<double> > df3 = copy(df0);

  double F0;
  double x1, x2, x4;
  double f1, f2, f3, f4;
  double d1, d2, d3, d4;
  double v1, v2, v3;
  double A, B;
  bool ls_failed = false;

  d4 = 0; f4 = 0; x4 = 0; // to fix a warning

  while( i < MAX_ITERS ){
    i++;

    // make a copy of current values
    dem0 = copy(*dem);
    F0 = f0;
    dF0 = copy(df0);

    double M = MAX;
    if(VERBOSE){
      printf("\tbeginning line search:\n");
      printf("\textrapolating: \n");
    }
    // Do some extrapolation
    while(true){
      x2 = 0;
      f2 = f0;
      f3 = f0;
      d2 = d0;

      df3 = 0*df3;
      tmp = copy(*dem);
      tmp = tmp + (x3*s);
      lossfun_accuracy(&f3, &df3, image_predicted, image, &tmp, init_dem, albedo, light_direction);
      M--;
      if(VERBOSE){
        printf("\t\tx = %f\tf = %f\n", x3, f3);
      }

      if(f3 < F0){ // keep best values
        dem0 = copy(*dem)+x3*s;
        F0 = f3;
        dF0 = copy(df3);
      }

      tmp = copy(s);
      tmp = tmp*df3;
      d3 = image_sum(&tmp); // new slope

      if( (d3 > SIG*d0) || (f3 > f0+x3*RHO*d0) || (M == 0)){
        // we are done extrapolating.
        // printf("done\n");
        break;
      }

      // move point 2 to point 1
      x1 = x2; f1 = f2; d1 = d2; df1 = copy(df2);

      // move point 3 to point 2
      x2 = x3; f2 = f3; d2 = d3; df2 = copy(df3);

      A = 6*(f1-f2)+3*(d2+d1)*(x2-x1); // make cubic extrapolation
      B = 3*(f2-f1)-(2*d1+d2)*(x2-x1);

      x3 = x1-d1*square(x2-x1)/(B+sqrt(B*B-A*d1*(x2-x1)));
      // printf("x3 = (%f -> %f)\n", x3_before, x3);

      if(isnan(x3) || isinf(x3) || (x3 < 0)){
        x3 = x2*EXT; // extrapolate maximum amount
      }else if(x3 > x2*EXT){ // new point beyond extrapolation limit?
        x3 = x2*EXT; // extrapolate maximum amount
      }else if(x3 < x2+INT*(x2-x1)){
        // new point too close to previous point
        x3 = x2+INT*(x2-x1);
      }
    }

    if(VERBOSE){
      printf("\tinterpolating: \n");
    }
    // Do some interpolation
    while( ((abs(d3) > -SIG*d0) || (f3 > f0+x3*RHO*d0)) && (M > 0) ){

      // choose subinterval
      if( (d3 > 0) || (f3 > f0+x3*RHO*d0) ){
        //move point 3 to point 4
        x4 = x3; f4 = f3; d4 = d3;
      }else{
        //move point 3 to point 2
        x2 = x3; f2 = f3; d2 = d3; df2 = copy(df3);
      }
      if(f4 > f0){
        // printf("quadratic interpolation\n");
        // quadratic interpolation
        x3 = x2-(0.5*d2*square(x4-x2))/(f4-f2-d2*(x4-x2));
      }else{
        // printf("cubic interpolation, %f, %f\n", f4, d4);
        // cubic interpolation
        A = 6*(f2-f4)/(x4-x2)+3*(d4+d2);
        B = 3*(f4-f2)-(2*d2+d4)*(x4-x2);
        x3 = x2+(sqrt(B*B-A*d2*square(x4-x2))-B)/A;
      }
      if(isnan(x3) || isinf(x3)){
        // printf("bisecting\n");
        x3 = (x2+x4)/2; // if we had a numerical problem then bisect
      }
      x3 = max(min(x3, x4-INT*(x4-x2)),x2+INT*(x4-x2)); // don't accept too close

      df3 = 0*df3;
      tmp = copy(*dem);
      tmp = tmp + (x3*s);
      lossfun_accuracy(&f3, &df3, image_predicted, image, &tmp, init_dem, albedo, light_direction);
      M--;

      if(VERBOSE){
        printf("\t\tx = %f\tf = %f\n", x3, f3);
      }

      // printf("interp: f=%f \t x3=(%f -> %f)\n", f3, x3_before, x3);

      if(f3 < F0){  // keep best values
        dem0 = copy(*dem);
        dem0 = dem0+x3*s;
        F0 = f3;
        dF0 = df3;
      }
      tmp = s*df3;
      d3 = image_sum(&tmp); // new slope
    }

    if(VERBOSE){
      printf("\tline search succeeded\n");
    }
    // if line search succeeded
    if( (abs(d3) < -SIG*d0) && (f3 < f0+x3*RHO*d0) ){
      (*dem) = (*dem) + x3*s;
      f0 = f3;

      if( abs((f0 - last_loss)/last_loss) <= (f_percent/100.0)){
        streak_count++;
      }else{
        streak_count = 0;
      }
      last_loss = f0;

      printf("%d:\tf = %f\n", i, f0 );
      if(streak_count >= f_streak){
        break;
      }

      tmp = df3 * df3;
      v1 = image_sum(&tmp);
      tmp = df0 * df3;
      v2 = image_sum(&tmp);
      tmp = df0 * df0;
      v3 = image_sum(&tmp);
      s = ((v1-v2)/(v3))*s - df3;  // Polack-Ribiere CG

      df0 = copy(df3); // swap derivatives
      tmp = copy(df0);
      tmp = tmp * s;
      d3 = d0;
      d0 = image_sum(&tmp);
      if(d0 > 0){ // new slope must be negative
        //otherwise use steepest direction
        s = -copy(df0);
        tmp = copy(s);
        tmp = tmp * tmp;
        d0 = -image_sum(&tmp);
      }
      x3 = x3 * min(RATIO, d3/(d0)); // should subtract epsilon from d0
      ls_failed = false;  // this line search did not fail
    }else{
      (*dem) = dem0; f0 = F0; df0 = copy(dF0); // restore best point so far
      if(ls_failed || (i > MAX_ITERS)){
        // line search failed twice in a row
        // or we ran out of time, so we give up
        printf("Giving up\n");
        break;
      }
      //try steepest
      s = -copy(df0);
      tmp = copy(s);
      tmp = tmp * tmp;
      d0 = -image_sum(&tmp);
      x3 = 1./(1.-d0);
      ls_failed = true;  // this line search failed
    }
  }

  end = clock();
  cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
  printf("Done in %f minutes\n", cpu_time_used/60);

}

/*
// This is mostly boilerplate for loading images from Matlab, and dumping the output to disk
int main(int argc, char *argv[]) {

// double sz[] = {3732, 4050};
double sz[] = {1866, 2025};

ImageView<PixelGray<double> > image(sz[0], sz[1]);
ImageView<PixelGray<double> > albedo(sz[0], sz[1]);
ImageView<PixelGray<double> > init_dem_point(sz[0], sz[1]);
ImageView<PixelGray<double> > init_dem(sz[0]+1, sz[1]+1);
char* image_filename = "../data/arc_apollo_dems/1135_1136-im_corrected.bin";
char* albedo_filename = "../data/arc_apollo_dems/1135_1136-albedo.bin";
char* init_dem_point_filename = "../data/arc_apollo_dems/1135_1136-dem.bin";
char* init_dem_filename = "../data/arc_apollo_dems/1135_1136-dem_grid.bin";


readBinaryImage(image, image_filename);
readBinaryImage(albedo, albedo_filename);
readBinaryImage(init_dem_point, init_dem_point_filename);
readBinaryImage(init_dem, init_dem_filename);

write_image( "output_hsv_dem_init.png", gray2hsv(init_dem_point, 10.));
// write_image( "output_dem_init.png", pixel_cast<float32>(softNormalize(init_dem_point, 3)) );

write_image( "output_image.png", pixel_cast<float32>(softNormalize(image, 6)) );
write_image( "output_albedo.png", pixel_cast<float32>(softNormalize(albedo, 6)) );
write_image( "output_dem_init.png", pixel_cast<float32>(softNormalize(init_dem, 6)) );

Vector<double, 3> light_direction(0.855353, 0.216011, 0.470862);

ImageView<PixelGray<double> > image_predicted(image.cols(), image.rows());
ImageView<PixelGray<double> > dem = copy(init_dem);

// optimize_check_gradient(&image_predicted, &image, &dem, &init_dem, &albedo, &light_direction);
// optimize_simple(&image_predicted, &image, &dem, &init_dem, &albedo, &light_direction);
optimize_conjugate_gradient(&image_predicted, &image, &dem, &init_dem, &albedo, &light_direction);

ImageView<PixelGray<double> > dem_output(sz[0], sz[1]);
dem_output = (crop(dem, 0, 0, image.cols(), image.rows()) + crop(dem, 1, 0, image.cols(), image.rows()) + crop(dem, 0, 1, image.cols(), image.rows()))/3.;

write_image( "output_predicted.png", pixel_cast<float32>(softNormalize(image_predicted, 3)) );
write_image( "output_dem_final.png", pixel_cast<float32>(softNormalize(dem_output, 3)) );
write_image( "output_hsv_dem.png", pixel_cast<PixelRGB<float32> >(gray2hsv(dem_output, 10.)));

return 0;
}
 */
