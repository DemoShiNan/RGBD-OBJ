/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MAP_H
#define MAP_H

#include "MapPoint.h"
#include "KeyFrame.h"
#include "MapPlane.h"
#include <set>
#include <unordered_map>

#include <mutex>
#include <pcl/common/transforms.h>
#include <pcl/point_types.h>
#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/ModelCoefficients.h>



namespace ORB_SLAM2
{

class MapPoint;
class KeyFrame;
class MapPlane;
class MapObject;
class Frame;

class Map
{
public:
    typedef pcl::PointXYZRGB PointT;
    typedef pcl::PointCloud <PointT> PointCloud;
    Map(const string &strSettingPath);
    // Map();

    void AddKeyFrame(KeyFrame* pKF);
    void AddMapPoint(MapPoint* pMP);
    void AddMapPlane(MapPlane* pMP);
    void AddNotSeenMapPlane(MapPlane* pMP);
    void EraseNotSeenMapPlane(MapPlane* pMP);

    std::vector<MapPlane*> GetAllMapPlanes();
    std::vector<MapPlane*> GetNotSeenMapPlanes();

    void EraseMapPoint(MapPoint* pMP);
    void EraseMapPlane(MapPlane* pMP);
    void EraseKeyFrame(KeyFrame* pKF);
    void SetReferenceMapPoints(const std::vector<MapPoint*> &vpMPs);
    void InformNewBigChange();
    int GetLastBigChangeIdx();

    void AssociatePlanesByBoundary(Frame &pF, bool out = false);
    void SearchMatchedPlanes(KeyFrame* pKF, cv::Mat Scw, const vector<MapPlane*> &vpPlanes,
                             vector<MapPlane*> &vpMatched,vector<MapPlane*> &vpMatchedPar,vector<MapPlane*> &vpMatchedVer,
                             bool out = false);

    double PointDistanceFromPlane(const cv::Mat& plane, PointCloud::Ptr boundry, bool out = false);

    std::vector<KeyFrame*> GetAllKeyFrames();
    std::vector<MapPoint*> GetAllMapPoints();
    std::vector<MapPoint*> GetReferenceMapPoints();
    std::vector<long unsigned int> GetRemovedPlanes();

    long unsigned int MapPointsInMap();
    long unsigned int MapPlanesInMap();
    long unsigned int NotSeenMapPlanesInMap();
    long unsigned  KeyFramesInMap();

    long unsigned int GetMaxKFid();

    void clear();

    vector<KeyFrame*> mvpKeyFrameOrigins;

    std::mutex mMutexMapUpdate;

    // This avoid that two points are created simultaneously in separate threads (id conflict)
    std::mutex mMutexPointCreation;

    // const std::unordered_map<unsigned int, Eigen::Matrix<double, 3, Eigen::Dynamic>>& GetAllMapObjectsPoints() {
    //     return ellipsoids_points_;
    // }

    void AddMapObject(MapObject *obj);
    void EraseMapObject(MapObject* obj);

    vector<MapObject*> GetAllMapObjects();

    size_t GetNumberMapObjects() const {
        return map_objects_.size();
    }
    size_t GetNumberPoints() const {
        return mspMapPoints.size();
    }

protected:
    std::set<MapPoint*> mspMapPoints;
    std::set<KeyFrame*> mspKeyFrames;

    std::set<MapPlane*> mspNotSeenMapPlanes;
    std::set<MapPlane*> mspMapPlanes;

    std::vector<MapPoint*> mvpReferenceMapPoints;
    std::vector<long unsigned int> mvnRemovedPlanes;

    long unsigned int mnMaxKFid;

    // Index related to a big change in the map (loop closure, global BA)
    int mnBigChangeIdx;

    std::mutex mMutexMap;

    float mfDisTh;
    float mfAngleTh;
    float mfVerTh;
    float mfParTh;
    // std::unordered_map<unsigned int, Eigen::Matrix<double, 3, Eigen::Dynamic>> ellipsoids_points_;
    std::set<MapObject*> map_objects_;
};

} //namespace ORB_SLAM

#endif // MAP_H
