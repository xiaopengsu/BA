#include <cmath>
#include <cstdio>
#include <iostream>
#include <fstream>
#include "ceres/ceres.h"
#include "ceres/rotation.h"
using ceres::CostFunction;
using ceres::CauchyLoss;

// Read a Bundle Adjustment in the Large dataset.
class BALProblem {
 public:
  ~BALProblem() {
    delete[] point_index_;
    delete[] camera_index_;
    delete[] observations_;
    delete[] parameters_;
    delete[] camera_0_observation;
  //  delete[] feature_color_;
  }

  int num_observations()       const { return num_observations_;               }
  int num_points()			   const { return num_points_;               }
  const double* observations() const { return observations_;                   }
  double* mutable_cameras()          { return parameters_;                     }
  double* mutable_points()           { return parameters_  + 9 * num_cameras_; }
  double camera_0_xy(int i)          { return camera_0_observation[i];         }
  int point_index(int i)		     { return  point_index_[i];  }
  int *feature_color(int i)          { return feature_color_+i*3;}
  double* mutable_camera_for_observation(int i) {
    return mutable_cameras() + camera_index_[i] * 9;
  }
  double* mutable_point_for_observation(int i) {
    return mutable_points() + point_index_[i] ;  // 3-->1
  }
  bool LoadFile(const char* filename) {
    FILE* fptr = fopen(filename, "r");
    if (fptr == NULL) {
      return false;
    };
    FscanfOrDie(fptr, "%d", &num_cameras_);
    FscanfOrDie(fptr, "%d", &num_points_);
    FscanfOrDie(fptr, "%d", &num_observations_);

    num_parameters_ = 9 * num_cameras_ + 1 * num_points_;  // 3-->1
    point_index_ = new int[num_observations_];
    camera_index_ = new int[num_observations_];
    observations_ = new double[2 * num_observations_];
    parameters_ = new double[num_parameters_];
    camera_0_observation = new double[2 * num_points_];
    feature_color_=new int[3*num_points_];

    int countforcamera0=0;
    for (int i = 0; i < num_observations_; ++i) {
      FscanfOrDie(fptr, "%d", camera_index_ + i);
      FscanfOrDie(fptr, "%d", point_index_ + i);
      for (int j = 0; j < 2; ++j) {
        FscanfOrDie(fptr, "%lf", observations_ + 2*i + j);
      }

      if (*(camera_index_+i)==0){
          camera_0_observation[countforcamera0]= *(observations_ + 2*i);
          camera_0_observation[countforcamera0+1]= *(observations_ + 2*i+1);
          countforcamera0=countforcamera0+2;
      }
    }
    for (int i = 0; i < num_parameters_; ++i) {
      FscanfOrDie(fptr, "%lf", parameters_ + i);
    }
   for (int i = 0; i < 3*num_points_; ++i) {
       FscanfOrDie(fptr, "%d", feature_color_ + i);
    }

    return true;
  }
 double* camera_0_observation;
 private:
  template<typename T>
  void FscanfOrDie(FILE *fptr, const char *format, T *value) {
    int num_scanned = fscanf(fptr, format, value);
    if (num_scanned != 1) {
      LOG(FATAL) << "Invalid UW data file.";
    }
  }
   int num_cameras_;
  int num_points_;
  int num_observations_;
  int num_parameters_;
  int* point_index_;
  int* camera_index_;
  double* observations_;
  double* parameters_;
  int* feature_color_;

} ;

// Templated pinhole camera model for used with Ceres.  The camera is
// parameterized using 9 parameters: 3 for rotation, 3 for translation, 1 for
// focal length and 2 for radial distortion. The principal point is not modeled
// (i.e. it is assumed be located at the image center).
struct WjError {
  WjError(double observed_x, double observed_y,double observed_img0_x,double observed_img0_y)
      : observed_x(observed_x), observed_y(observed_y),
        observed_img0_x(observed_img0_x), observed_img0_y(observed_img0_y)  {}

  template <typename T>
  bool operator()(const T* const camera,
                  const T* const point,
                  const T* const onlyfocal,
                  T* residuals) const {


                const T& focal = T(onlyfocal[0]);
     //       const T& focal=T(2700);
                T xj= T(observed_img0_x)/(-focal );
                T yj = T(observed_img0_y)/(-focal);
//              const T& xj = point[0];
//              const T& yj = point[1];
              const T& wj = point[0];

             // T xj= T(observed_img0_x)/(-focal); T yj = T(observed_img0_y)/(-focal);
              T pxi = T(observed_x);	 T pyi = T(observed_y);

                  const T& theta_x =camera[0];
                  const T& theta_y =camera[1];
                  const T& theta_z =camera[2];
                  const T& txi =camera[3];
                  const T& tyi =camera[4];
                  const T& tzi =camera[5];
                  T axi = xj - theta_z*yj+ theta_y;
                  T bxi = txi;
                  T ayi = yj - theta_x+ theta_z*xj ;
                  T byi = tyi;
                  T ci  = -theta_y*xj+theta_x*yj+T(1);
                  T di  = tzi;
              T exi = pxi*ci - axi*(-focal);    //focal_length
              T fxi = pxi*di - bxi*(-focal);
              T eyi = pyi*ci - ayi*(-focal);
              T fyi = pyi*di - byi*(-focal);

              residuals[0] = (exi+fxi*wj)/(ci+di*wj);
              residuals[1] = (eyi+fyi*wj)/(ci+di*wj);

    return true;
  }

  // Factory to hide the construction of the CostFunction object from
  // the client code.
  static ceres::CostFunction* Create(const double observed_x,const double observed_y,
                                     const double observed_img0_x,const double observed_img0_y) {
    return (new ceres::AutoDiffCostFunction<WjError, 2, 9, 1, 1>(
                new WjError(observed_x, observed_y,observed_img0_x,observed_img0_y)));
  }

  double observed_x;
  double observed_y;
  double observed_img0_x;
  double observed_img0_y;
 };


void writeplywj( char* outfile_name,BALProblem& bal_problem,float focal){
     std::ofstream myfile;
    myfile.open (outfile_name);
    myfile<<"ply \nformat ascii 1.0 \nelement vertex "<<bal_problem.num_points()<<"\nproperty float x \n";
    myfile<<"property float y \nproperty float z\nproperty uchar red \nproperty uchar green \nproperty uchar blue \nend_header\n";

    for (int i = 0; i < bal_problem.num_points(); i++){

        double wj=(*(bal_problem.mutable_point_for_observation(0)+i));
        double tempx= bal_problem.camera_0_xy(2*i);
        double tempy= bal_problem.camera_0_xy(2*i+1);
        double xj= tempx/((-focal)*wj);
        double yj= tempy/((-focal)*wj);
        double zj= 1/wj;
        int red =*(bal_problem.feature_color(i));
        int green =*(bal_problem.feature_color(i)+1);
        int blue =*(bal_problem.feature_color(i)+2);
        if(zj<20&&zj>0){
        myfile <<xj<<" "<<yj<<" "<<zj<<" ";
        myfile <<red<<" "<<green<<" "<<blue<<"\n";
        }
    }
    for (int i=0;i<100;i++){
   myfile <<0<<" "<<0<<" "<<i*0.01<<" "<<255-i<<" "<<0<<" "<<100<<"\n";
    }
    myfile.close();
}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  if (argc <2) {
    std::cerr << "usage: simple_bundle_adjuster <bal_problem>\n";
    return 1;
  }

  BALProblem bal_problem;
  if (!bal_problem.LoadFile(argv[1])) {
    std::cerr << "ERROR: unable to open file " << argv[1] << "\n";
    return 1;
  }
  int fuck=0;
  double *onlyfocal = new double [1];
  onlyfocal[0]=2700;

  const double* observations = bal_problem.observations();

  // Create residuals for each observation in the bundle adjustment problem. The
  // parameters for cameras and points are added automatically.
  ceres::Problem problem;

  for (int i = 0; i < bal_problem.num_observations(); ++i) {
    // Each Residual block takes a point and a camera as input and outputs a 2
    // dimensional residual. Internally, the cost function stores the observed
    // image location and compares the reprojection against the observation.

      int j=bal_problem.point_index(i);
      double observations0x=  bal_problem.camera_0_xy(2*j);
      double observations0y=  bal_problem.camera_0_xy(2*j+1);
     float errorx = abs(observations0x-observations[2 * i + 0]);
     float errory = abs(observations0y-observations[2 * i + 1]);
     if (errorx>30 || errory>30){
     fuck++;
     }

     ceres::CostFunction* cost_function =
         WjError::Create(observations[2 * i + 0], observations[2 * i + 1],observations0x,observations0y);

     problem.AddResidualBlock(cost_function,
                              NULL /* squared loss */,
                              bal_problem.mutable_camera_for_observation(i),
                              bal_problem.mutable_point_for_observation(i),
                              onlyfocal
                              );
  }
  std::cout<<"outlier_num="<<fuck<<std::endl;
  // Make Ceres automatically detect the bundle structure. Note that the
  // standard solver, SPARSE_NORMAL_CHOLESKY, also works fine but it is slower
  // for standard bundle adjustment problems.
  ceres::Solver::Options options;
  options.linear_solver_type = ceres::DENSE_SCHUR;
  options.max_num_iterations=500;
  options.minimizer_progress_to_stdout = true;

  ceres::Solver::Summary summary;
  ceres::Solve(options, &problem, &summary);
  std::cout << summary.FullReport() << "\n";

  writeplywj(argv[2],bal_problem,onlyfocal[0]);

  std::ofstream myfile;

  myfile.open ("../txt/ARTreport.txt");
  myfile<<onlyfocal[0]<<std::endl;
  double *campara=bal_problem.mutable_camera_for_observation(0);
  for (int i = 0; i < 200; i++){ myfile <<*(campara+i)<<"\n";}
   myfile.close();


  return 0;
}
