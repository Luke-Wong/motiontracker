#include <iostream>
#include <boost/thread/thread.hpp>
#include <boost/lexical_cast.hpp>
#include "motiontracker.hh"


MotionTracker::MotionTracker(Webcam &webcam, const CalibrationParameters &calibParams)
	: WebcamListener(webcam), m_calibParams(calibParams), m_pos(), m_rot(), m_rotm(cv::Mat::eye(3,3,CV_32F)), m_counter(5)
{ }

cv::Vec3f MotionTracker::getRotation() const {
	boost::mutex::scoped_lock l(m_mutex);
	return m_rot;
}

cv::Mat MotionTracker::getRotationMatrix() const {
	boost::mutex::scoped_lock l(m_mutex);
	return m_rotm;
}

cv::Vec3f MotionTracker::getPosition() const {
	boost::mutex::scoped_lock l(m_mutex);
	return m_pos;
}

int MotionTracker::getFPS() const {
	boost::mutex::scoped_lock l(m_mutex);
	return m_counter.getFPS();
}



ChessboardTracker::ChessboardTracker(Webcam &webcam, const CalibrationParameters &calibParams)
	: MotionTracker(webcam, calibParams), m_boardScaleFactor(25), m_boardH(9), m_numCorners(6*9), m_boardSize(cv::Size(6,9))
{
	for (int i = 0; i < m_numCorners; ++i)
		m_objectCorners.push_back(cv::Point3f(m_boardScaleFactor*(i / m_boardH), m_boardScaleFactor*(i % m_boardH), 0.0f));
}

void ChessboardTracker::frameEvent(const cv::Mat& frame) {
	// Find the chessboard
	bool patternFound = cv::findChessboardCorners(frame, m_boardSize, m_corners, cv::CALIB_CB_FAST_CHECK);

	// Solve the pose
	cv::Mat pos, rot;
	if (patternFound && (int)m_corners.size() == m_numCorners) {
		cv::solvePnP(cv::Mat(m_objectCorners), cv::Mat(m_corners), m_calibParams.intrinsic_parameters, m_calibParams.distortion_coeffs, rot, pos, false);

		// Assign new values
		boost::mutex::scoped_lock l(m_mutex);
		m_pos = cv::Vec3f(pos.at<double>(0,0), pos.at<double>(0,1), pos.at<double>(0,2));
		m_rot = cv::Vec3f(rot.at<double>(0,0), rot.at<double>(0,1), rot.at<double>(0,2));
		m_counter(); // Update FPS
	}
}




ColorTracker::ColorTracker(Webcam &webcam, int hue)
	: MotionTracker(webcam), m_hue(hue)
{ }

void ColorTracker::frameEvent(const cv::Mat& frame) {
	const int dH = 20;

	// Solve the position
	cv::Mat imgHSV, thresh;
	cv::cvtColor(frame, imgHSV, CV_BGR2HSV); // Switch to HSV color space
	cv::inRange(imgHSV, cv::Scalar(m_hue - dH, 120, 120), cv::Scalar(m_hue + dH, 255, 255), thresh);

	// Calculate the moments to estimate the position
	cv::Moments m = cv::moments(thresh, true);
	int x = m.m10 / m.m00;
	int y = m.m01 / m.m00;

	// Assign new values
	boost::mutex::scoped_lock l(m_mutex);
	m_pos = cv::Vec3f(x, y, 0);
	m_counter(); // Update FPS
}

ColorCrossTracker::ColorCrossTracker(Webcam &webcam, const CalibrationParameters &calibParams, int solver)
	: MotionTracker(webcam), m_calibParams(calibParams), m_solver(solver)
{
	m_objectPoints.push_back(cv::Point3f(0,0,0));   // Green ("Origin")
	m_objectPoints.push_back(cv::Point3f(0,100,0)); // Red
	m_objectPoints.push_back(cv::Point3f(100,0,0)); // Blue
	m_objectPoints.push_back(cv::Point3f(0,0,100)); // Yellow

	m_modelPoints.push_back(cvPoint3D32f(0.0f, 0.0f, 0.0f));
	m_modelPoints.push_back(cvPoint3D32f(0.0f, 100.0f, 0.0f));
	m_modelPoints.push_back(cvPoint3D32f(100.0f, 0.0f, 0.0f));
	m_modelPoints.push_back(cvPoint3D32f(0.0f, 0.0f, 100.0f));

	m_positObject = cvCreatePOSITObject(&m_modelPoints[0], (int)m_modelPoints.size() );
}

std::vector<cv::Point2f> ColorCrossTracker::getImagePoints() const {
	boost::mutex::scoped_lock l(m_mutex);
	return m_savedImagePoints;
}

std::vector<cv::Point2f> ColorCrossTracker::getProjectedPoints() const {
	boost::mutex::scoped_lock l(m_mutex);
	return m_savedProjectedPoints;
}

void ColorCrossTracker::frameEvent(const cv::Mat& frame) {
	// Solve image points
	m_imagePoints.clear();
	m_srcImagePoints.clear();
	m_projectedPoints.clear();

	cv::Mat imgHSV;
	cv::cvtColor(frame, imgHSV, CV_BGR2HSV); // Switch to HSV color space

	// Image points must be added in the same order as model points
	// Also, all of them must be found or the calculations are aborted

	if (
		calculateImagePoint(imgHSV, m_calibParams.hues.at<int>(0), m_calibParams.dHues.at<int>(0),
							m_calibParams.satval_l.at<int>(0), m_calibParams.satval_h.at<int>(0)) &&  // Green
		calculateImagePoint(imgHSV, m_calibParams.hues.at<int>(1), m_calibParams.dHues.at<int>(1),
							m_calibParams.satval_l.at<int>(1), m_calibParams.satval_h.at<int>(1)) && // Red
		calculateImagePoint(imgHSV, m_calibParams.hues.at<int>(2), m_calibParams.dHues.at<int>(2),
							m_calibParams.satval_l.at<int>(2), m_calibParams.satval_h.at<int>(2)) && // Blue
		calculateImagePoint(imgHSV, m_calibParams.hues.at<int>(3), m_calibParams.dHues.at<int>(3),
							m_calibParams.satval_l.at<int>(3), m_calibParams.satval_h.at<int>(3))     // Yellow
		)
	{
		// Solve pose
		if (m_solver == 1) solvePnP();
		else if (m_solver == 2) solvePOSIT();
		else throw std::runtime_error("Bad solver type in ColorCrossTracker");
	}

	m_counter(); // Update FPS
}

bool ColorCrossTracker::calculateImagePoint(const cv::Mat& frame, const int hue, const int dHue, const int satval_low, const int satval_high) {
	// Threshold
	cv::Mat thresh;
	cv::inRange(frame, cv::Scalar(hue - dHue, satval_low, satval_low), cv::Scalar(hue + dHue, satval_high, satval_high), thresh);

	// Calculate the moments to estimate the position
	cv::Moments m = cv::moments(thresh, true);
	int x = m.m10 / m.m00;
	int y = m.m01 / m.m00;

	if (x < 0 || y < 0) return false; // Not found

	// Save point
	m_imagePoints.push_back(cv::Point2f(x,y));
	m_srcImagePoints.push_back( cvPoint2D32f( x, y) );

	return true;
}

void ColorCrossTracker::solvePnP() {
	cv::Mat rvec, tvec;

	// Calculate translation and rotation vectors
	cv::solvePnP(cv::Mat(m_objectPoints), cv::Mat(m_imagePoints), m_calibParams.intrinsic_parameters, m_calibParams.distortion_coeffs,rvec,tvec,false);

	// Project model points to image plane using calculated translation and rotation vectors
	cv::projectPoints(cv::Mat(m_objectPoints),rvec,tvec,m_calibParams.intrinsic_parameters, m_calibParams.distortion_coeffs, m_projectedPoints);

	// Assign new values
	boost::mutex::scoped_lock l(m_mutex);
	m_pos = tvec;
	m_rot = rvec;
	m_savedImagePoints = m_imagePoints;
	m_savedProjectedPoints = m_projectedPoints;


}

void ColorCrossTracker::solvePOSIT() {

	const int FOCAL_LENGTH = 1000;

	float rotation_matrix[9];
	float translation_vector[3];
	CvTermCriteria criteria = cvTermCriteria(CV_TERMCRIT_EPS | CV_TERMCRIT_ITER, 100, 1.0e-4f);
	cvPOSIT(m_positObject, &m_srcImagePoints[0], FOCAL_LENGTH, criteria, rotation_matrix, translation_vector);

	cv::Mat rotm(3, 3, CV_32FC1, rotation_matrix);
	cv::Mat rot;
	cv::Rodrigues(rotm, rot);

	for (size_t p = 0; p < m_modelPoints.size(); ++p) {
		cv::Point3f point3D;
		point3D.x = rotation_matrix[0] * m_modelPoints[p].x +
			rotation_matrix[1] * m_modelPoints[p].y +
			rotation_matrix[2] * m_modelPoints[p].z +
			translation_vector[0];
		point3D.y = rotation_matrix[3] * m_modelPoints[p].x +
			rotation_matrix[4] * m_modelPoints[p].y +
			rotation_matrix[5] * m_modelPoints[p].z +
			translation_vector[1];
		point3D.z = rotation_matrix[6] * m_modelPoints[p].x +
			rotation_matrix[7] * m_modelPoints[p].y +
			rotation_matrix[8] * m_modelPoints[p].z +
			translation_vector[2];
		cv::Point2f point2D( 0.0, 0.0 );
		if ( point3D.z != 0 )
		{
			point2D.x = FOCAL_LENGTH * point3D.x / point3D.z;
			point2D.y = FOCAL_LENGTH * point3D.y / point3D.z;
		}
		m_projectedPoints.push_back( point2D );
	}

	// Assign new values
	boost::mutex::scoped_lock l(m_mutex);
	m_pos = cv::Vec3f(translation_vector[0],translation_vector[1],translation_vector[2]);
	m_rot = rot;
	m_rotm = rotm;
	m_savedImagePoints = m_imagePoints;
	m_savedProjectedPoints = m_projectedPoints;
}
