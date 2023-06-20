﻿/**
* This file is part of OA-SLAM.
*
* Copyright (C) 2022 Matthieu Zins <matthieu.zins@inria.fr>
* (Inria, LORIA, Université de Lorraine)
* OA-SLAM is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* OA-SLAM is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with OA-SLAM. If not, see <http://www.gnu.org/licenses/>.
*/


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

#include "Optimizer.h"

#include "Thirdparty/g2o/g2o/core/block_solver.h"
#include "Thirdparty/g2o/g2o/core/optimization_algorithm_levenberg.h"
#include "Thirdparty/g2o/g2o/solvers/linear_solver_eigen.h"
#include "Thirdparty/g2o/g2o/types/types_six_dof_expmap.h"
#include "Thirdparty/g2o/g2o/core/robust_kernel_impl.h"
#include "Thirdparty/g2o/g2o/solvers/linear_solver_dense.h"
#include "Thirdparty/g2o/g2o/types/types_seven_dof_expmap.h"

#include<Eigen/StdVector>

#include "Converter.h"
#include "OptimizerObject.h"
#include "g2oAddition/Plane3D.h"
#include "g2oAddition/EdgePlane.h"
#include "g2oAddition/VertexPlane.h"
#include "g2oAddition/EdgeVerticalPlane.h"
#include "g2oAddition/EdgeParallelPlane.h"
#include "g2oAddition/EdgeTwoVerPlanes.h"
#include "g2oAddition/EdgeTwoParPlanes.h"
#include<mutex>

namespace ORB_SLAM2
{


void Optimizer::GlobalBundleAdjustemnt(Map* pMap, int nIterations, bool* pbStopFlag, const unsigned long nLoopKF, const bool bRobust)
{
    vector<KeyFrame*> vpKFs = pMap->GetAllKeyFrames();
    vector<MapPoint*> vpMP = pMap->GetAllMapPoints();
    vector<MapPlane*> vpMPl = pMap->GetAllMapPlanes();
    vector<MapPlane*> vpNMPl = pMap->GetNotSeenMapPlanes();
    BundleAdjustment(vpKFs,vpMP,vpMPl,vpNMPl,nIterations,pbStopFlag, nLoopKF, bRobust);
}

//modify-oa
void Optimizer::BundleAdjustment(const std::vector<KeyFrame*> &vpKFs ,const std::vector<MapPoint*> &vpMP,
                                 const std::vector<MapPlane*> &vpMPl, const std::vector<MapPlane*> &vpNMPl,
                                 int nIterations = 5, bool *pbStopFlag=NULL, const unsigned long nLoopKF=0,
                                 const bool bRobust = true)
{
    vector<bool> vbNotIncludedMP;
    vbNotIncludedMP.resize(vpMP.size());

    g2o::SparseOptimizer optimizer;
    g2o::BlockSolver_6_3::LinearSolverType * linearSolver;

    linearSolver = new g2o::LinearSolverEigen<g2o::BlockSolver_6_3::PoseMatrixType>();

    g2o::BlockSolver_6_3 * solver_ptr = new g2o::BlockSolver_6_3(linearSolver);

    g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
    optimizer.setAlgorithm(solver);

    if(pbStopFlag)
        optimizer.setForceStopFlag(pbStopFlag);

    long unsigned int maxKFid = 0;
    long unsigned int maxMPid = 0;
    long unsigned int maxMPlid = 0;


    // Set KeyFrame vertices
    for(size_t i=0; i<vpKFs.size(); i++)
    {
        KeyFrame* pKF = vpKFs[i];
        if(pKF->isBad())
            continue;
        g2o::VertexSE3Expmap * vSE3 = new g2o::VertexSE3Expmap();
        vSE3->setEstimate(Converter::toSE3Quat(pKF->GetPose()));
        vSE3->setId(pKF->mnId);
        vSE3->setFixed(pKF->mnId==0);
        optimizer.addVertex(vSE3);
        if(pKF->mnId>maxKFid)
            maxKFid=pKF->mnId;
    }

    const float thHuber2D = sqrt(5.99);
    const float thHuber3D = sqrt(7.815);

    // Set MapPoint vertices
    for(size_t i=0; i<vpMP.size(); i++)
    {
        MapPoint* pMP = vpMP[i];
        if(pMP->isBad())
            continue;
        g2o::VertexSBAPointXYZ* vPoint = new g2o::VertexSBAPointXYZ();
        vPoint->setEstimate(Converter::toVector3d(pMP->GetWorldPos()));
        const int id = pMP->mnId+maxKFid+1;
        vPoint->setId(id);
        vPoint->setMarginalized(true);
        optimizer.addVertex(vPoint);
        if(id > maxMPid)
            maxMPid = id;
       const map<KeyFrame*,size_t> observations = pMP->GetObservations();

        int nEdges = 0;
        //SET EDGES
        for(map<KeyFrame*,size_t>::const_iterator mit=observations.begin(); mit!=observations.end(); mit++)
        {

            KeyFrame* pKF = mit->first;
            if(pKF->isBad() || pKF->mnId>maxKFid)
                continue;

            nEdges++;

            const cv::KeyPoint &kpUn = pKF->mvKeysUn[mit->second];

            if(pKF->mvuRight[mit->second]<0)
            {
                Eigen::Matrix<double,2,1> obs;
                obs << kpUn.pt.x, kpUn.pt.y;

                g2o::EdgeSE3ProjectXYZ* e = new g2o::EdgeSE3ProjectXYZ();

                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(pKF->mnId)));
                e->setMeasurement(obs);
                const float &invSigma2 = pKF->mvInvLevelSigma2[kpUn.octave];
                e->setInformation(Eigen::Matrix2d::Identity()*invSigma2);

                if(bRobust)
                {
                    g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
                    e->setRobustKernel(rk);
                    rk->setDelta(thHuber2D);
                }

                e->fx = pKF->fx;
                e->fy = pKF->fy;
                e->cx = pKF->cx;
                e->cy = pKF->cy;

                optimizer.addEdge(e);
            }
            else
            {
                Eigen::Matrix<double,3,1> obs;
                const float kp_ur = pKF->mvuRight[mit->second];
                obs << kpUn.pt.x, kpUn.pt.y, kp_ur;

                g2o::EdgeStereoSE3ProjectXYZ* e = new g2o::EdgeStereoSE3ProjectXYZ();

                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(pKF->mnId)));
                e->setMeasurement(obs);
                const float &invSigma2 = pKF->mvInvLevelSigma2[kpUn.octave];
                Eigen::Matrix3d Info = Eigen::Matrix3d::Identity()*invSigma2;
                e->setInformation(Info);

                if(bRobust)
                {
                    g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
                    e->setRobustKernel(rk);
                    rk->setDelta(thHuber3D);
                }

                e->fx = pKF->fx;
                e->fy = pKF->fy;
                e->cx = pKF->cx;
                e->cy = pKF->cy;
                e->bf = pKF->mbf;

                optimizer.addEdge(e);
            }
        }

        if(nEdges==0)
        {
            optimizer.removeVertex(vPoint);
            vbNotIncludedMP[i]=true;
        }
        else
        {
            vbNotIncludedMP[i]=false;
        }
    }

   double angleInfo = Config::Get<double>("Plane.AngleInfo");
    angleInfo = 3282.8/(angleInfo*angleInfo);
    double disInfo = Config::Get<double>("Plane.DistanceInfo");
    disInfo = disInfo* disInfo;
    double parInfo = Config::Get<double>("Plane.ParallelInfo");
    parInfo = 3282.8/(parInfo*parInfo);
    double verInfo = Config::Get<double>("Plane.VerticalInfo");
    verInfo = 3282.8/(verInfo*verInfo);
    double planeChi = Config::Get<double>("Plane.Chi");
    const float deltaPlane = sqrt(planeChi);
    double VPplaneChi = Config::Get<double>("Plane.VPChi");
    const float VPdeltaPlane = sqrt(VPplaneChi);

    //Set MapPlane vertices
    for(size_t i=0; i<vpMPl.size(); i++) {
        MapPlane *pMP = vpMPl[i];
        if (pMP->isBad())
            continue;
        g2o::VertexPlane *vPlane = new g2o::VertexPlane();
        vPlane->setEstimate(Converter::toPlane3D(pMP->GetWorldPos()));
        int id = pMP->mnId + maxMPid + 1;
        vPlane->setId(id);
        vPlane->setMarginalized(true);
        optimizer.addVertex(vPlane);
        if(id > maxMPlid)
            maxMPlid = id;

        //SET EDGES
        const map<KeyFrame *, int> observations = pMP->GetObservations();
        for (map<KeyFrame *, int>::const_iterator mit = observations.begin(), mend = observations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgePlane *e = new g2o::EdgePlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix3d Info;
                Info << angleInfo, 0, 0,
                        0, angleInfo, 0,
                        0, 0, disInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(deltaPlane);
                optimizer.addEdge(e);
            }
        }
        //SET EDGES
        const map<KeyFrame *, int> notseenobservations = pMP->GetNotSeenObservations();
        for (map<KeyFrame *, int>::const_iterator mit = notseenobservations.begin(), mend = notseenobservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgePlane *e = new g2o::EdgePlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvNotSeenPlaneCoefficients[mit->second]));
                Eigen::Matrix3d Info;
                Info << angleInfo/2, 0, 0,
                        0, angleInfo/2, 0,
                        0, 0, disInfo/2;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(deltaPlane/2);
                optimizer.addEdge(e);
            }
        }
        //SET EDGES
        const map<KeyFrame *, int> verObservations = pMP->GetVerObservations();
        for (map<KeyFrame *, int>::const_iterator mit = verObservations.begin(), mend = verObservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgeVerticalPlane *e = new g2o::EdgeVerticalPlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix2d Info;
                Info << verInfo, 0,
                        0, verInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(VPdeltaPlane);
                optimizer.addEdge(e);
            }
        }
        //SET EDGES
        const map<KeyFrame *, int> parObservations = pMP->GetParObservations();
        for (map<KeyFrame *, int>::const_iterator mit = parObservations.begin(), mend = parObservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgeParallelPlane *e = new g2o::EdgeParallelPlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix2d Info;
                Info << parInfo, 0,
                        0, parInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(VPdeltaPlane);
                optimizer.addEdge(e);
            }
        }
    }

    //Set NotSeen MapPlane vertices
    for(size_t i=0; i<vpNMPl.size(); i++) {
        MapPlane *pMP = vpNMPl[i];
        if (pMP->isBad())
            continue;
        g2o::VertexPlane *vPlane = new g2o::VertexPlane();
        vPlane->setEstimate(Converter::toPlane3D(pMP->GetWorldPos()));
        int id = pMP->mnId + maxMPlid + 1;
        vPlane->setId(id);
        vPlane->setMarginalized(true);
        optimizer.addVertex(vPlane);
        //SET EDGES
        const map<KeyFrame *, int> observations = pMP->GetObservations();
        for (map<KeyFrame *, int>::const_iterator mit = observations.begin(), mend = observations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgePlane *e = new g2o::EdgePlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix3d Info;
                Info << angleInfo, 0, 0,
                        0, angleInfo, 0,
                        0, 0, disInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(deltaPlane);
                optimizer.addEdge(e);
            }
        }
        //SET EDGES
        const map<KeyFrame *, int> notseenobservations = pMP->GetNotSeenObservations();
        for (map<KeyFrame *, int>::const_iterator mit = notseenobservations.begin(), mend = notseenobservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgePlane *e = new g2o::EdgePlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvNotSeenPlaneCoefficients[mit->second]));
                Eigen::Matrix3d Info;
                Info << angleInfo/2, 0, 0,
                        0, angleInfo/2, 0,
                        0, 0, disInfo/2;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(deltaPlane/2);
                optimizer.addEdge(e);
            }
        }
        //SET EDGES
        const map<KeyFrame *, int> verObservations = pMP->GetVerObservations();
        for (map<KeyFrame *, int>::const_iterator mit = verObservations.begin(), mend = verObservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgeVerticalPlane *e = new g2o::EdgeVerticalPlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix2d Info;
                Info << verInfo, 0,
                        0, verInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(VPdeltaPlane);
                optimizer.addEdge(e);
            }
        }
        //SET EDGES
        const map<KeyFrame *, int> parObservations = pMP->GetParObservations();
        for (map<KeyFrame *, int>::const_iterator mit = parObservations.begin(), mend = parObservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgeParallelPlane *e = new g2o::EdgeParallelPlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix2d Info;
                Info << parInfo, 0,
                        0, parInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(VPdeltaPlane);
                optimizer.addEdge(e);
            }
        }
    }

    // Optimize!
    optimizer.initializeOptimization();
    optimizer.optimize(nIterations);

    // Recover optimized data

    //Keyframes
    for(size_t i=0; i<vpKFs.size(); i++)
    {
        KeyFrame* pKF = vpKFs[i];
        if(pKF->isBad())
            continue;
        g2o::VertexSE3Expmap* vSE3 = static_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(pKF->mnId));
        g2o::SE3Quat SE3quat = vSE3->estimate();
        if(nLoopKF==0)
        {
            pKF->SetPose(Converter::toCvMat(SE3quat));
        }
        else
        {
            pKF->mTcwGBA.create(4,4,CV_32F);
            Converter::toCvMat(SE3quat).copyTo(pKF->mTcwGBA);
            pKF->mnBAGlobalForKF = nLoopKF;
        }
    }

    //Points
    for(size_t i=0; i<vpMP.size(); i++)
    {
        if(vbNotIncludedMP[i])
            continue;

        MapPoint* pMP = vpMP[i];

        if(pMP->isBad())
            continue;
        g2o::VertexSBAPointXYZ* vPoint = static_cast<g2o::VertexSBAPointXYZ*>(optimizer.vertex(pMP->mnId+maxKFid+1));

        if(nLoopKF==0)
        {
            pMP->SetWorldPos(Converter::toCvMat(vPoint->estimate()));
            pMP->UpdateNormalAndDepth();
        }
        else
        {
            pMP->mPosGBA.create(3,1,CV_32F);
            Converter::toCvMat(vPoint->estimate()).copyTo(pMP->mPosGBA);
            pMP->mnBAGlobalForKF = nLoopKF;
        }
    }

    //Planes
   for(size_t i=0; i<vpMPl.size(); i++)
    {
        MapPlane* pMP = vpMPl[i];

        if(pMP->isBad())
            continue;
        g2o::VertexPlane* vPlane = static_cast<g2o::VertexPlane*>(optimizer.vertex(pMP->mnId+maxMPid+1));

        if(nLoopKF==0)
        {
            pMP->SetWorldPos(Converter::toCvMat(vPlane->estimate()));
        }
        else
        {
            pMP->mPosGBA.create(4,1,CV_32F);
            Converter::toCvMat(vPlane->estimate()).copyTo(pMP->mPosGBA);
            pMP->mnBAGlobalForKF = nLoopKF;
        }
    }

    //Not Seen Planes
    for(size_t i=0; i<vpNMPl.size(); i++)
    {
        MapPlane* pMP = vpNMPl[i];

        if(pMP->isBad())
            continue;
        g2o::VertexPlane* vPlane = static_cast<g2o::VertexPlane*>(optimizer.vertex(pMP->mnId+maxMPlid+1));

        if(nLoopKF==0)
        {
            pMP->SetWorldPos(Converter::toCvMat(vPlane->estimate()));
        }
        else
        {
            pMP->mPosGBA.create(4,1,CV_32F);
            Converter::toCvMat(vPlane->estimate()).copyTo(pMP->mPosGBA);
            pMP->mnBAGlobalForKF = nLoopKF;
        }
    }
}

//modify-oa
int Optimizer::PoseOptimization(Frame *pFrame)
{
    g2o::SparseOptimizer optimizer;
    g2o::BlockSolver_6_3::LinearSolverType * linearSolver;

    linearSolver = new g2o::LinearSolverDense<g2o::BlockSolver_6_3::PoseMatrixType>();

    g2o::BlockSolver_6_3 * solver_ptr = new g2o::BlockSolver_6_3(linearSolver);

    g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
    optimizer.setAlgorithm(solver);

    int nInitialCorrespondences=0;

    // Set Frame vertex
    g2o::VertexSE3Expmap * vSE3 = new g2o::VertexSE3Expmap();
    vSE3->setEstimate(Converter::toSE3Quat(pFrame->mTcw));
    vSE3->setId(0);
    vSE3->setFixed(false);
    optimizer.addVertex(vSE3);

    // Set MapPoint vertices
    const int N = pFrame->N;

    vector<g2o::EdgeSE3ProjectXYZOnlyPose*> vpEdgesMono;
    vector<size_t> vnIndexEdgeMono;
    vpEdgesMono.reserve(N);
    vnIndexEdgeMono.reserve(N);

    vector<g2o::EdgeStereoSE3ProjectXYZOnlyPose*> vpEdgesStereo;
    vector<size_t> vnIndexEdgeStereo;
    vpEdgesStereo.reserve(N);
    vnIndexEdgeStereo.reserve(N);

    const float deltaMono = sqrt(5.991);
    const float deltaStereo = sqrt(7.815);

    {
    unique_lock<mutex> lock(MapPoint::mGlobalMutex);
    int BANum = 0;
    double BAEror = 0, BAMax = 0;
    for(int i=0; i<N; i++)
    {
        MapPoint* pMP = pFrame->mvpMapPoints[i];
        if(pMP)
        {
            // Monocular observation
            if(pFrame->mvuRight[i]<0)
            {
                nInitialCorrespondences++;
                pFrame->mvbOutlier[i] = false;

                Eigen::Matrix<double,2,1> obs;
                const cv::KeyPoint &kpUn = pFrame->mvKeysUn[i];
                obs << kpUn.pt.x, kpUn.pt.y;

                g2o::EdgeSE3ProjectXYZOnlyPose* e = new g2o::EdgeSE3ProjectXYZOnlyPose();

                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(0)));
                e->setMeasurement(obs);
                const float invSigma2 = pFrame->mvInvLevelSigma2[kpUn.octave];
                e->setInformation(Eigen::Matrix2d::Identity()*invSigma2);

                g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(deltaMono);

                e->fx = pFrame->fx;
                e->fy = pFrame->fy;
                e->cx = pFrame->cx;
                e->cy = pFrame->cy;
                cv::Mat Xw = pMP->GetWorldPos();
                e->Xw[0] = Xw.at<float>(0);
                e->Xw[1] = Xw.at<float>(1);
                e->Xw[2] = Xw.at<float>(2);

                optimizer.addEdge(e);

                vpEdgesMono.push_back(e);
                vnIndexEdgeMono.push_back(i);
            }
            else  // Stereo observation
            {
                nInitialCorrespondences++;
                pFrame->mvbOutlier[i] = false;

                //SET EDGE
                Eigen::Matrix<double,3,1> obs;
                const cv::KeyPoint &kpUn = pFrame->mvKeysUn[i];
                const float &kp_ur = pFrame->mvuRight[i];
                obs << kpUn.pt.x, kpUn.pt.y, kp_ur;

                g2o::EdgeStereoSE3ProjectXYZOnlyPose* e = new g2o::EdgeStereoSE3ProjectXYZOnlyPose();

                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(0)));
                e->setMeasurement(obs);
                const float invSigma2 = pFrame->mvInvLevelSigma2[kpUn.octave];
                Eigen::Matrix3d Info = Eigen::Matrix3d::Identity()*invSigma2;
                e->setInformation(Info);

                g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(deltaStereo);

                e->fx = pFrame->fx;
                e->fy = pFrame->fy;
                e->cx = pFrame->cx;
                e->cy = pFrame->cy;
                e->bf = pFrame->mbf;
                cv::Mat Xw = pMP->GetWorldPos();
                e->Xw[0] = Xw.at<float>(0);
                e->Xw[1] = Xw.at<float>(1);
                e->Xw[2] = Xw.at<float>(2);

                optimizer.addEdge(e);

                vpEdgesStereo.push_back(e);
                vnIndexEdgeStereo.push_back(i);
            }
        }

    }
    
    }


    if(nInitialCorrespondences<3)
        return 0;

    //Set Plane vertices
    const int M = pFrame->mnPlaneNum;
    vector<g2o::EdgePlane*> vpEdgesPlane;
    vector<size_t> vnIndexEdgePlane;
    vpEdgesPlane.reserve(M);
    vnIndexEdgePlane.reserve(M);

    vector<g2o::EdgeParallelPlane*> vpEdgesParPlane;
    vector<size_t> vnIndexEdgeParPlane;
    vpEdgesParPlane.reserve(M);
    vnIndexEdgeParPlane.reserve(M);

    vector<g2o::EdgeVerticalPlane*> vpEdgesVerPlane;
    vector<size_t> vnIndexEdgeVerPlane;
    vpEdgesVerPlane.reserve(M);
    vnIndexEdgeVerPlane.reserve(M);

    vector<g2o::EdgePlane*> vpEdgesNotSeenPlane;
    vector<size_t> vnIndexEdgeNotSeenPlane;
    vpEdgesNotSeenPlane.reserve(pFrame->mnNotSeenPlaneNum);
    vnIndexEdgeNotSeenPlane.reserve(pFrame->mnNotSeenPlaneNum);

    std::set<int> vnVertexId;

    double angleInfo = Config::Get<double>("Plane.AngleInfo");
    angleInfo = 3282.8/(angleInfo*angleInfo);
    double disInfo = Config::Get<double>("Plane.DistanceInfo");
    disInfo = disInfo* disInfo;
    double parInfo = Config::Get<double>("Plane.ParallelInfo");
    parInfo = 3282.8/(parInfo*parInfo);
    double verInfo = Config::Get<double>("Plane.VerticalInfo");
    verInfo = 3282.8/(verInfo*verInfo);
    double planeChi = Config::Get<double>("Plane.Chi");
    const float deltaPlane = sqrt(planeChi);

    double VPplaneChi = Config::Get<double>("Plane.VPChi");
    const float VPdeltaPlane = sqrt(VPplaneChi);

    {
        unique_lock<mutex> lock(MapPlane::mGlobalMutex);
        int PNum = 0;
        double PEror = 0, PMax = 0;
        unsigned long maxPlaneid = 0;
        for (int i = 0; i < M; ++i) {
            MapPlane* pMP = pFrame->mvpMapPlanes[i];
            if(pMP){
//                cout << "add plane vertex: " << pMP->mnId;
                pFrame->mvbPlaneOutlier[i] = false;
                if(vnVertexId.count(pMP->mnId) == 0) {
                    g2o::VertexPlane* vP = new g2o::VertexPlane();
                    vP->setEstimate(Converter::toPlane3D(pMP->GetWorldPos()));
                    int id = pMP->mnId + 1;
                    if (id > maxPlaneid)
                        maxPlaneid = id;
                    vP->setId(id);
                    vP->setFixed(true);
                    optimizer.addVertex(vP);
                    vnVertexId.insert(pMP->mnId);
                }
                nInitialCorrespondences++;
                g2o::EdgePlane* e = new g2o::EdgePlane();
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(0)));
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(pMP->mnId + 1)));
                e->setMeasurement(Converter::toPlane3D(pFrame->mvPlaneCoefficients[i]));
                //TODO
                Eigen::Matrix3d Info;
                if(pMP->mbSeen) {
                    Info << angleInfo, 0, 0,
                            0, angleInfo, 0,
                            0, 0, disInfo;
                }
                else{
                    Info << 2*angleInfo, 0, 0,
                            0, 2*angleInfo, 0,
                            0, 0, 2*disInfo;
                }

                e->setInformation(Info);

                g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                //TODO
                rk->setDelta(deltaPlane);

                optimizer.addEdge(e);

                vpEdgesPlane.push_back(e);
                vnIndexEdgePlane.push_back(i);

//                e->computeError();
//                double chi = e->chi2();
//                PEror += chi;
//                PMax = PMax > chi ? PMax : chi;
//                PNum ++;
//                cout << "  done!" << endl;
            }
        }
//        cout << " Plane: " << PEror/PNum << " ";//" Max: " << PMax << " ";

        PNum = 0;
        PEror = 0;
        PMax = 0;
        vnVertexId.clear();
        unsigned long maxParallelPlaneId = maxPlaneid;
        for (int i = 0; i < M; ++i) {
            // add parallel planes!
            MapPlane *pMP = pFrame->mvpParallelPlanes[i];
            if (pMP) {
                pFrame->mvbParPlaneOutlier[i] = false;
                int id = pMP->mnId + maxPlaneid + 1;

                if(vnVertexId.count(pMP->mnId) == 0) {
//                cout << "add parallel plane " << pMP->mnId << endl;
                    g2o::VertexPlane *vP = new g2o::VertexPlane();
                    vP->setEstimate(Converter::toPlane3D(pMP->GetWorldPos()));
                    if (id > maxParallelPlaneId)
                        maxParallelPlaneId = id;
                    vP->setId(id);
                    vP->setFixed(true);
                    optimizer.addVertex(vP);
                    vnVertexId.insert(pMP->mnId);
                }
                nInitialCorrespondences++;
                g2o::EdgeParallelPlane *e = new g2o::EdgeParallelPlane();
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(0)));
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setMeasurement(Converter::toPlane3D(pFrame->mvPlaneCoefficients[i]));
                //TODO
                Eigen::Matrix2d Info;
                Info << parInfo, 0,
                        0, parInfo;

                e->setInformation(Info);

                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                //TODO
                rk->setDelta(VPdeltaPlane);
                optimizer.addEdge(e);

                vpEdgesParPlane.push_back(e);
                vnIndexEdgeParPlane.push_back(i);

//                e->computeError();
//                double chi = e->chi2();
//                PEror += chi;
//                PMax = PMax > chi ? PMax : chi;
//                PNum ++;
            }
        }
//        cout << " Par Plane: " << PEror/PNum << " ";//" Max: " << PMax << " ";
        PNum = 0;
        PEror = 0;
        PMax = 0;
        vnVertexId.clear();

        unsigned long maxVerticalPlaneId = maxParallelPlaneId;
        for (int i = 0; i < M; ++i) {
            // add vertical planes!
            MapPlane* pMP = pFrame->mvpVerticalPlanes[i];
            if(pMP){
                pFrame->mvbVerPlaneOutlier[i] = false;
                int id = pMP->mnId + maxParallelPlaneId + 1;

                if(vnVertexId.count(pMP->mnId) == 0) {
//                cout << "add vertical plane " << pMP->mnId << endl;
                    g2o::VertexPlane *vP = new g2o::VertexPlane();
                    vP->setEstimate(Converter::toPlane3D(pMP->GetWorldPos()));
                    if(id > maxVerticalPlaneId)
                        maxVerticalPlaneId = id;
                    vP->setId(id);
                    vP->setFixed(true);
                    optimizer.addVertex(vP);
                    vnVertexId.insert(pMP->mnId);
                }
                nInitialCorrespondences++;
                g2o::EdgeVerticalPlane* e = new g2o::EdgeVerticalPlane();
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(0)));
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id)));
                e->setMeasurement(Converter::toPlane3D(pFrame->mvPlaneCoefficients[i]));
                //TODO
                Eigen::Matrix2d Info;
                Info << verInfo, 0,
                        0, verInfo;

                e->setInformation(Info);

                g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                //TODO
                rk->setDelta(VPdeltaPlane);
                optimizer.addEdge(e);

                vpEdgesVerPlane.push_back(e);
                vnIndexEdgeVerPlane.push_back(i);

//                e->computeError();
//                double chi = e->chi2();
//                PEror += chi;
//                PMax = PMax > chi ? PMax : chi;
//                PNum ++;
            }
        }
//        cout << " Ver Plane: " << PEror/PNum << endl;//" Max: " << PMax << endl;
        vnVertexId.clear();
        for (int i = 0; i < pFrame->mnNotSeenPlaneNum; ++i) {
            MapPlane* pMP = pFrame->mvpNotSeenMapPlanes[i];
            if(pMP){
                pFrame->mvbNotSeenPlaneOutlier[i] = false;
                int id = pMP->mnId + maxVerticalPlaneId + 1;
                if(vnVertexId.count(pMP->mnId) == 0) {
                    g2o::VertexPlane* vP = new g2o::VertexPlane();
                    vP->setEstimate(Converter::toPlane3D(pMP->GetWorldPos()));
                    vP->setId(id);
                    vP->setFixed(true);
                    optimizer.addVertex(vP);
                    vnVertexId.insert(pMP->mnId);
                }
                nInitialCorrespondences++;
                g2o::EdgePlane* e = new g2o::EdgePlane();
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(0)));
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id)));
                e->setMeasurement(Converter::toPlane3D(pFrame->mvNotSeenPlaneCoefficients[i]));
                //TODO
                Eigen::Matrix3d Info;
                if(pMP->mbSeen) {
                    Info << angleInfo/2, 0, 0,
                            0, angleInfo/2, 0,
                            0, 0, disInfo/2;
                }
                else{
                    Info << angleInfo/4, 0, 0,
                            0, angleInfo/4, 0,
                            0, 0, disInfo/4;
                }

                e->setInformation(Info);

                g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                //TODO
                rk->setDelta(deltaPlane/2);

                optimizer.addEdge(e);

                vpEdgesNotSeenPlane.push_back(e);
                vnIndexEdgeNotSeenPlane.push_back(i);
            }
        }

    }

    // We perform 4 optimizations, after each optimization we classify observation as inlier/outlier
    // At the next optimization, outliers are not included, but at the end they can be classified as inliers again.
    const float chi2Mono[4]={5.991,5.991,5.991,5.991};
    const float chi2Stereo[4]={7.815,7.815,7.815, 7.815};
    const int its[4]={10,10,10,10};    

    int nBad=0;
    for(size_t it=0; it<4; it++)
    {

        vSE3->setEstimate(Converter::toSE3Quat(pFrame->mTcw));
        optimizer.initializeOptimization(0);
        optimizer.optimize(its[it]);

        nBad=0;
        for(size_t i=0, iend=vpEdgesMono.size(); i<iend; i++)
        {
            g2o::EdgeSE3ProjectXYZOnlyPose* e = vpEdgesMono[i];

            const size_t idx = vnIndexEdgeMono[i];

            if(pFrame->mvbOutlier[idx])
            {
                e->computeError();
            }

            const float chi2 = e->chi2();

            if(chi2>chi2Mono[it])
            {                
                pFrame->mvbOutlier[idx]=true;
                e->setLevel(1);
                nBad++;
            }
            else
            {
                pFrame->mvbOutlier[idx]=false;
                e->setLevel(0);
            }

            if(it==2)
                e->setRobustKernel(0);
        }

        int BAN = 0;
        double BAE = 0, BAMax = 0;
        for(size_t i=0, iend=vpEdgesStereo.size(); i<iend; i++)
        {
            g2o::EdgeStereoSE3ProjectXYZOnlyPose* e = vpEdgesStereo[i];

            const size_t idx = vnIndexEdgeStereo[i];

            if(pFrame->mvbOutlier[idx])
            {
                e->computeError();
            }

            const float chi2 = e->chi2();
            BAN ++ ;
            BAE += chi2;
            BAMax = BAMax > chi2 ? BAMax : chi2;

            if(chi2>chi2Stereo[it])
            {
                pFrame->mvbOutlier[idx]=true;
                e->setLevel(1);
                nBad++;
            }
            else
            {                
                e->setLevel(0);
                pFrame->mvbOutlier[idx]=false;
            }

            if(it==2)
                e->setRobustKernel(0);
        }
  
        int PN = 0;
        double PE = 0, PMax = 0;

        for(size_t i=0, iend=vpEdgesPlane.size(); i<iend; i++)
        {
            g2o::EdgePlane* e = vpEdgesPlane[i];

            const size_t idx = vnIndexEdgePlane[i];

            if(pFrame->mvbPlaneOutlier[idx])
            {
                e->computeError();
            }

            const float chi2 = e->chi2();
            PN ++ ;
            PE += chi2;
            PMax = PMax > chi2 ? PMax : chi2;

            if(chi2>planeChi)
            {
                pFrame->mvbPlaneOutlier[idx]=true;
                e->setLevel(1);
                nBad++;
//                cout << "bad: " << chi2 << "  Pc : " << pFrame->ComputePlaneWorldCoeff(idx).t() << "  Pw :" << pFrame->mvpMapPlanes[idx]->GetWorldPos().t() << endl;
            }
            else
            {
                e->setLevel(0);
                pFrame->mvbPlaneOutlier[idx]=false;
            }

            if(it==2)
                e->setRobustKernel(0);
        }
//        if(PN==0)
//            cout << "No plane " << " ";
//        else
//            cout << " Plane: " << PE/PN << " "; //<< " Max: " << PMax << endl;

        for(size_t i=0, iend=vpEdgesNotSeenPlane.size(); i<iend; i++)
        {
            g2o::EdgePlane* e = vpEdgesNotSeenPlane[i];

            const size_t idx = vnIndexEdgeNotSeenPlane[i];

            if(pFrame->mvbNotSeenPlaneOutlier[idx])
            {
                e->computeError();
            }

            const float chi2 = e->chi2();
            PN ++ ;
            PE += chi2;
            PMax = PMax > chi2 ? PMax : chi2;
            if(chi2>planeChi/2)
            {
                pFrame->mvbNotSeenPlaneOutlier[idx]=true;
                e->setLevel(1);
                nBad++;
//                cout << "bad: " << chi2 << "  Pc : " << pFrame->ComputePlaneWorldCoeff(idx).t() << "  Pw :" << pFrame->mvpMapPlanes[idx]->GetWorldPos().t() << endl;
            }
            else
            {
                e->setLevel(0);
                pFrame->mvbNotSeenPlaneOutlier[idx]=false;
            }

            if(it==2)
                e->setRobustKernel(0);
        }

        PN = 0;
        PE = 0;
        PMax = 0;
        for(size_t i=0, iend=vpEdgesParPlane.size(); i<iend; i++)
        {
            g2o::EdgeParallelPlane* e = vpEdgesParPlane[i];

            const size_t idx = vnIndexEdgeParPlane[i];

            if(pFrame->mvbParPlaneOutlier[idx])
            {
                e->computeError();
            }

            const float chi2 = e->chi2();
            PN ++ ;
            PE += chi2;
            PMax = PMax > chi2 ? PMax : chi2;

            if(chi2>VPplaneChi)
            {
                pFrame->mvbParPlaneOutlier[idx]=true;
                e->setLevel(1);
                nBad++;
//                cout << "bad Par: " << chi2 << "  Pc : " << pFrame->ComputePlaneWorldCoeff(idx).t() << "  Pw :" << pFrame->mvpParallelPlanes[idx]->GetWorldPos().t() << endl;
            }
            else
            {
                e->setLevel(0);
                pFrame->mvbParPlaneOutlier[idx]=false;
            }

            if(it==2)
                e->setRobustKernel(0);
        }
//        if(PN==0)
//            cout << "No par plane " << " ";
//        else
//            cout << "par Plane: " << PE/PN << " "; //<< " Max: " << PMax << endl;

        PN = 0;
        PE = 0;
        PMax = 0;
        for(size_t i=0, iend=vpEdgesVerPlane.size(); i<iend; i++)
        {
            g2o::EdgeVerticalPlane* e = vpEdgesVerPlane[i];

            const size_t idx = vnIndexEdgeVerPlane[i];

            if(pFrame->mvbVerPlaneOutlier[idx])
            {
                e->computeError();
            }

            const float chi2 = e->chi2();
            PN ++ ;
            PE += chi2;
            PMax = PMax > chi2 ? PMax : chi2;

            if(chi2>VPplaneChi)
            {
                pFrame->mvbVerPlaneOutlier[idx]=true;
                e->setLevel(1);
                nBad++;
//                cout << "bad Ver: " << chi2 << "  Pc : " << pFrame->ComputePlaneWorldCoeff(idx).t() << "  Pw :" << pFrame->mvpVerticalPlanes[idx]->GetWorldPos().t() << endl;
            }
            else
            {
                e->setLevel(0);
                pFrame->mvbVerPlaneOutlier[idx]=false;
            }

            if(it==2)
                e->setRobustKernel(0);
        }

        if(optimizer.edges().size()<10)
            break;
    }
    // Recover optimized pose and return number of inliers
    g2o::VertexSE3Expmap* vSE3_recov = static_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(0));
    g2o::SE3Quat SE3quat_recov = vSE3_recov->estimate();
    cv::Mat pose = Converter::toCvMat(SE3quat_recov);
    pFrame->SetPose(pose);

    return nInitialCorrespondences-nBad;
}

void Optimizer::LocalBundleAdjustment(KeyFrame *pKF, bool* pbStopFlag, Map* pMap, int use_objects)
{    
    // Local KeyFrames: First Breath Search from Current Keyframe
    list<KeyFrame*> lLocalKeyFrames;

    lLocalKeyFrames.push_back(pKF);
    pKF->mnBALocalForKF = pKF->mnId;

    const vector<KeyFrame*> vNeighKFs = pKF->GetVectorCovisibleKeyFrames();
    for(int i=0, iend=vNeighKFs.size(); i<iend; i++)
    {
        KeyFrame* pKFi = vNeighKFs[i];
        pKFi->mnBALocalForKF = pKF->mnId;
        if(!pKFi->isBad())
            lLocalKeyFrames.push_back(pKFi);
    }

    // Local MapPoints seen in Local KeyFrames
    list<MapPoint*> lLocalMapPoints;
    list<MapPlane*> lLocalMapPlanes;
    list<MapPlane*> lLocalNotSeenMapPlanes;
    for(list<KeyFrame*>::iterator lit=lLocalKeyFrames.begin() , lend=lLocalKeyFrames.end(); lit!=lend; lit++)
    {
        vector<MapPoint*> vpMPs = (*lit)->GetMapPointMatches();
        for(vector<MapPoint*>::iterator vit=vpMPs.begin(), vend=vpMPs.end(); vit!=vend; vit++)
        {
            MapPoint* pMP = *vit;
            if(pMP)
                if(!pMP->isBad())
                    if(pMP->mnBALocalForKF!=pKF->mnId)
                    {
                        lLocalMapPoints.push_back(pMP);
                        pMP->mnBALocalForKF=pKF->mnId;
                    }
        }

        for (int i = 0; i < (*lit)->mnPlaneNum; ++i) {
            MapPlane* pMP = (*lit)->mvpMapPlanes[i];
            if(pMP)
                if(pMP->mnBALocalForKF!=pKF->mnId){
                    lLocalMapPlanes.push_back(pMP);
                    pMP->mnBALocalForKF = pKF->mnId;
                }
        }
    }

    for(list<KeyFrame*>::iterator lit=lLocalKeyFrames.begin() , lend=lLocalKeyFrames.end(); lit!=lend; lit++) {
        for (int i = 0; i < (*lit)->mnNotSeenPlaneNum; ++i) {
            MapPlane *pMP = (*lit)->mvpNotSeenMapPlanes[i];
            if (pMP)
                if (pMP->mnBALocalForKF != pKF->mnId) {
                    lLocalNotSeenMapPlanes.push_back(pMP);
                    pMP->mnBALocalForKF = pKF->mnId;
                }
        }
    }

    // Fixed Keyframes. Keyframes that see Local MapPoints but that are not Local Keyframes
    list<KeyFrame*> lFixedCameras;
    for(list<MapPoint*>::iterator lit=lLocalMapPoints.begin(), lend=lLocalMapPoints.end(); lit!=lend; lit++)
    {
        map<KeyFrame*,size_t> observations = (*lit)->GetObservations();
        for(map<KeyFrame*,size_t>::iterator mit=observations.begin(), mend=observations.end(); mit!=mend; mit++)
        {
            KeyFrame* pKFi = mit->first;

            if(pKFi->mnBALocalForKF!=pKF->mnId && pKFi->mnBAFixedForKF!=pKF->mnId)
            {                
                pKFi->mnBAFixedForKF=pKF->mnId;
                if(!pKFi->isBad())
                    lFixedCameras.push_back(pKFi);
            }
        }
    }

    // Fixed Keyframes. Keyframes that see Local MapPlanes but that are not Local Keyframes
    for(list<MapPlane*>::iterator lit=lLocalMapPlanes.begin(), lend=lLocalMapPlanes.end(); lit!=lend; lit++){
        map<KeyFrame*,int> observations = (*lit)->GetObservations();
        for(map<KeyFrame*,int>::iterator mit=observations.begin(), mend=observations.end(); mit!=mend; mit++)
        {
            KeyFrame* pKFi = mit->first;

            if(pKFi->mnBALocalForKF!=pKF->mnId && pKFi->mnBAFixedForKF!=pKF->mnId)
            {
                pKFi->mnBAFixedForKF=pKF->mnId;
                if(!pKFi->isBad())
                    lFixedCameras.push_back(pKFi);
            }
        }
            map<KeyFrame*,int> notseenobservations = (*lit)->GetNotSeenObservations();
        for(map<KeyFrame*,int>::iterator mit=notseenobservations.begin(), mend=notseenobservations.end(); mit!=mend; mit++)
        {
            KeyFrame* pKFi = mit->first;

            if(pKFi->mnBALocalForKF!=pKF->mnId && pKFi->mnBAFixedForKF!=pKF->mnId)
            {
                pKFi->mnBAFixedForKF=pKF->mnId;
                if(!pKFi->isBad())
                    lFixedCameras.push_back(pKFi);
            }
        }

        map<KeyFrame*,int> verObservations = (*lit)->GetVerObservations();
        for(map<KeyFrame*,int>::iterator mit=verObservations.begin(), mend=verObservations.end(); mit!=mend; mit++)
        {
            KeyFrame* pKFi = mit->first;

            if(pKFi->mnBALocalForKF!=pKF->mnId && pKFi->mnBAFixedForKF!=pKF->mnId)
            {
                pKFi->mnBAFixedForKF=pKF->mnId;
                if(!pKFi->isBad())
                    lFixedCameras.push_back(pKFi);
            }
        }
        map<KeyFrame*,int> parObservations = (*lit)->GetParObservations();
        for(map<KeyFrame*,int>::iterator mit=parObservations.begin(), mend=parObservations.end(); mit!=mend; mit++)
        {
            KeyFrame* pKFi = mit->first;

            if(pKFi->mnBALocalForKF!=pKF->mnId && pKFi->mnBAFixedForKF!=pKF->mnId)
            {
                pKFi->mnBAFixedForKF=pKF->mnId;
                if(!pKFi->isBad())
                    lFixedCameras.push_back(pKFi);
            }
        }
    }
    
     // Fixed Keyframes. Keyframes that see Local MapPlanes but that are not Local Keyframes
    for(list<MapPlane*>::iterator lit=lLocalNotSeenMapPlanes.begin(), lend=lLocalNotSeenMapPlanes.end(); lit!=lend; lit++){
        map<KeyFrame*,int> observations = (*lit)->GetObservations();
        for(map<KeyFrame*,int>::iterator mit=observations.begin(), mend=observations.end(); mit!=mend; mit++)
        {
            KeyFrame* pKFi = mit->first;

            if(pKFi->mnBALocalForKF!=pKF->mnId && pKFi->mnBAFixedForKF!=pKF->mnId)
            {
                pKFi->mnBAFixedForKF=pKF->mnId;
                if(!pKFi->isBad())
                    lFixedCameras.push_back(pKFi);
            }
        }

        map<KeyFrame*,int> notseenobservations = (*lit)->GetNotSeenObservations();
        for(map<KeyFrame*,int>::iterator mit=notseenobservations.begin(), mend=notseenobservations.end(); mit!=mend; mit++)
        {
            KeyFrame* pKFi = mit->first;

            if(pKFi->mnBALocalForKF!=pKF->mnId && pKFi->mnBAFixedForKF!=pKF->mnId)
            {
                pKFi->mnBAFixedForKF=pKF->mnId;
                if(!pKFi->isBad())
                    lFixedCameras.push_back(pKFi);
            }
        }

        map<KeyFrame*,int> verObservations = (*lit)->GetVerObservations();
        for(map<KeyFrame*,int>::iterator mit=verObservations.begin(), mend=verObservations.end(); mit!=mend; mit++)
        {
            KeyFrame* pKFi = mit->first;

            if(pKFi->mnBALocalForKF!=pKF->mnId && pKFi->mnBAFixedForKF!=pKF->mnId)
            {
                pKFi->mnBAFixedForKF=pKF->mnId;
                if(!pKFi->isBad())
                    lFixedCameras.push_back(pKFi);
            }
        }
        map<KeyFrame*,int> parObservations = (*lit)->GetParObservations();
        for(map<KeyFrame*,int>::iterator mit=parObservations.begin(), mend=parObservations.end(); mit!=mend; mit++)
        {
            KeyFrame* pKFi = mit->first;

            if(pKFi->mnBALocalForKF!=pKF->mnId && pKFi->mnBAFixedForKF!=pKF->mnId)
            {
                pKFi->mnBAFixedForKF=pKF->mnId;
                if(!pKFi->isBad())
                    lFixedCameras.push_back(pKFi);
            }
        }
    }


    // Setup optimizer
    g2o::SparseOptimizer optimizer;
    g2o::Solver *solver_ptr = nullptr;

    if (use_objects <= 1) {
        // cout<<"in LBA :nomal-type use_obj<=1 "<<endl;
        g2o::BlockSolver_6_3::LinearSolverType * linearSolver;
        linearSolver = new g2o::LinearSolverEigen<g2o::BlockSolver_6_3::PoseMatrixType>();
        solver_ptr = new g2o::BlockSolver_6_3(linearSolver);
    } else {
        // cout<<"in LBA :abnomal-type g2o BlockSolverX  "<<endl;
        g2o::BlockSolverX::LinearSolverType *linearSolver;
        linearSolver = new g2o::LinearSolverEigen<g2o::BlockSolverX::PoseMatrixType>();
        solver_ptr = new g2o::BlockSolverX(linearSolver);
    }


    g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
    optimizer.setAlgorithm(solver);

    if(pbStopFlag)
        optimizer.setForceStopFlag(pbStopFlag);

    unsigned long maxKFid = 0;

    // Set Local KeyFrame vertices
    for(list<KeyFrame*>::iterator lit=lLocalKeyFrames.begin(), lend=lLocalKeyFrames.end(); lit!=lend; lit++)
    {
        KeyFrame* pKFi = *lit;
        g2o::VertexSE3Expmap * vSE3 = new g2o::VertexSE3Expmap();
        vSE3->setEstimate(Converter::toSE3Quat(pKFi->GetPose()));
        vSE3->setId(pKFi->mnId);
        vSE3->setFixed(pKFi->mnId==0);
        optimizer.addVertex(vSE3);
        if(pKFi->mnId>maxKFid)
            maxKFid=pKFi->mnId;
    }

    // Set Fixed KeyFrame vertices
    for(list<KeyFrame*>::iterator lit=lFixedCameras.begin(), lend=lFixedCameras.end(); lit!=lend; lit++)
    {
        KeyFrame* pKFi = *lit;
        g2o::VertexSE3Expmap * vSE3 = new g2o::VertexSE3Expmap();
        vSE3->setEstimate(Converter::toSE3Quat(pKFi->GetPose()));
        vSE3->setId(pKFi->mnId);
        vSE3->setFixed(true);
        optimizer.addVertex(vSE3);
        if(pKFi->mnId>maxKFid)
            maxKFid=pKFi->mnId;
    }

    // Set MapPoint vertices
    const int nExpectedSize = (lLocalKeyFrames.size()+lFixedCameras.size())*lLocalMapPoints.size();

    vector<g2o::EdgeSE3ProjectXYZ*> vpEdgesMono;
    vpEdgesMono.reserve(nExpectedSize);

    vector<KeyFrame*> vpEdgeKFMono;
    vpEdgeKFMono.reserve(nExpectedSize);

    vector<MapPoint*> vpMapPointEdgeMono;
    vpMapPointEdgeMono.reserve(nExpectedSize);

    vector<g2o::EdgeStereoSE3ProjectXYZ*> vpEdgesStereo;
    vpEdgesStereo.reserve(nExpectedSize);

    vector<KeyFrame*> vpEdgeKFStereo;
    vpEdgeKFStereo.reserve(nExpectedSize);

    vector<MapPoint*> vpMapPointEdgeStereo;
    vpMapPointEdgeStereo.reserve(nExpectedSize);

    const float thHuberMono = sqrt(5.991);
    const float thHuberStereo = sqrt(7.815);
    unsigned long maxPointid = 0;

    for(list<MapPoint*>::iterator lit=lLocalMapPoints.begin(), lend=lLocalMapPoints.end(); lit!=lend; lit++)
    {
        MapPoint* pMP = *lit;
        g2o::VertexSBAPointXYZ* vPoint = new g2o::VertexSBAPointXYZ();
        vPoint->setEstimate(Converter::toVector3d(pMP->GetWorldPos()));
        int id = pMP->mnId+maxKFid+1;
        if(id > maxPointid)
            maxPointid = id;
        vPoint->setId(id);
        vPoint->setMarginalized(true);
        optimizer.addVertex(vPoint);

        const map<KeyFrame*,size_t> observations = pMP->GetObservations();

        //Set edges
        for(map<KeyFrame*,size_t>::const_iterator mit=observations.begin(), mend=observations.end(); mit!=mend; mit++)
        {
            KeyFrame* pKFi = mit->first;

            if(!pKFi->isBad())
            {                
                const cv::KeyPoint &kpUn = pKFi->mvKeysUn[mit->second];

                // Monocular observation
                if(pKFi->mvuRight[mit->second]<0)
                {
                    Eigen::Matrix<double,2,1> obs;
                    obs << kpUn.pt.x, kpUn.pt.y;

                    g2o::EdgeSE3ProjectXYZ* e = new g2o::EdgeSE3ProjectXYZ();

                    e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id)));
                    e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(pKFi->mnId)));
                    e->setMeasurement(obs);
                    const float &invSigma2 = pKFi->mvInvLevelSigma2[kpUn.octave];
                    e->setInformation(Eigen::Matrix2d::Identity()*invSigma2);

                    g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
                    e->setRobustKernel(rk);
                    rk->setDelta(thHuberMono);

                    e->fx = pKFi->fx;
                    e->fy = pKFi->fy;
                    e->cx = pKFi->cx;
                    e->cy = pKFi->cy;

                    optimizer.addEdge(e);
                    vpEdgesMono.push_back(e);
                    vpEdgeKFMono.push_back(pKFi);
                    vpMapPointEdgeMono.push_back(pMP);
                }
                else // Stereo observation
                {
                    Eigen::Matrix<double,3,1> obs;
                    const float kp_ur = pKFi->mvuRight[mit->second];
                    obs << kpUn.pt.x, kpUn.pt.y, kp_ur;

                    g2o::EdgeStereoSE3ProjectXYZ* e = new g2o::EdgeStereoSE3ProjectXYZ();

                    e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id)));
                    e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(pKFi->mnId)));
                    e->setMeasurement(obs);
                    const float &invSigma2 = pKFi->mvInvLevelSigma2[kpUn.octave];
                    Eigen::Matrix3d Info = Eigen::Matrix3d::Identity()*invSigma2;
                    e->setInformation(Info);

                    g2o::RobustKernelHuber* rk = new g2o::RobustKernelHuber;
                    e->setRobustKernel(rk);
                    rk->setDelta(thHuberStereo);

                    e->fx = pKFi->fx;
                    e->fy = pKFi->fy;
                    e->cx = pKFi->cx;
                    e->cy = pKFi->cy;
                    e->bf = pKFi->mbf;

                    optimizer.addEdge(e);
                    vpEdgesStereo.push_back(e);
                    vpEdgeKFStereo.push_back(pKFi);
                    vpMapPointEdgeStereo.push_back(pMP);
                }
            }
        }
    }

    //---------------plane---------------------------
    const int nExpectedPlaneEdgeSize = (lLocalKeyFrames.size()+lFixedCameras.size())*(lLocalMapPlanes.size()+lLocalNotSeenMapPlanes.size());
    vector<g2o::EdgePlane*> vpEdgesPlane;
    vpEdgesPlane.reserve(nExpectedPlaneEdgeSize);

    vector<g2o::EdgePlane*> vpEdgesNotSeenPlane;
    vpEdgesNotSeenPlane.reserve(nExpectedPlaneEdgeSize);

    vector<g2o::EdgeParallelPlane*> vpEdgesParPlane;
    vpEdgesParPlane.reserve(nExpectedPlaneEdgeSize);

    vector<g2o::EdgeVerticalPlane*> vpEdgesVerPlane;
    vpEdgesVerPlane.reserve(nExpectedPlaneEdgeSize);

    vector<KeyFrame*> vpEdgeKFPlane;
    vpEdgeKFPlane.reserve(nExpectedPlaneEdgeSize);

    vector<MapPlane*> vpMapPlane;
    vpMapPlane.reserve(nExpectedPlaneEdgeSize);

    vector<KeyFrame*> vpEdgeKFNotSeenPlane;
    vpEdgeKFNotSeenPlane.reserve(nExpectedPlaneEdgeSize);

    vector<MapPlane*> vpNotSeenMapPlane;
    vpNotSeenMapPlane.reserve(nExpectedPlaneEdgeSize);

    double angleInfo = Config::Get<double>("Plane.AngleInfo");
    angleInfo = 3282.8/(angleInfo*angleInfo);
    double disInfo = Config::Get<double>("Plane.DistanceInfo");
    disInfo = disInfo* disInfo;
    double parInfo = Config::Get<double>("Plane.ParallelInfo");
    parInfo = 3282.8/(parInfo*parInfo);
    double verInfo = Config::Get<double>("Plane.VerticalInfo");
    verInfo = 3282.8/(verInfo*verInfo);
    double planeChi = Config::Get<double>("Plane.Chi");
    const float deltaPlane = sqrt(planeChi);
    double VPplaneChi = Config::Get<double>("Plane.VPChi");
    const float VPdeltaPlane = sqrt(VPplaneChi);

    for (list<MapPlane *>::iterator lit = lLocalMapPlanes.begin(), lend = lLocalMapPlanes.end();
         lit != lend; lit++) {

        MapPlane *pMP = *lit;
        g2o::VertexPlane *vPlane = new g2o::VertexPlane();
        vPlane->setEstimate(Converter::toPlane3D(pMP->GetWorldPos()));
        int id = pMP->mnId + maxPointid + 1;
        vPlane->setId(id);
        vPlane->setMarginalized(true);
        optimizer.addVertex(vPlane);

        const map<KeyFrame *, int> observations = pMP->GetObservations();
        for (map<KeyFrame *, int>::const_iterator mit = observations.begin(), mend = observations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgePlane *e = new g2o::EdgePlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix3d Info;
                Info << angleInfo, 0, 0,
                        0, angleInfo, 0,
                        0, 0, disInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(deltaPlane);
                optimizer.addEdge(e);

                vpEdgesPlane.push_back(e);
                vpEdgeKFPlane.push_back(pKFi);
                vpMapPlane.push_back(pMP);
            }
        }

        const map<KeyFrame *, int> notseenobservations = pMP->GetNotSeenObservations();
        for (map<KeyFrame *, int>::const_iterator mit = notseenobservations.begin(), mend = notseenobservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgePlane *e = new g2o::EdgePlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvNotSeenPlaneCoefficients[mit->second]));
                Eigen::Matrix3d Info;
                Info << angleInfo/2, 0, 0,
                        0, angleInfo/2, 0,
                        0, 0, disInfo/2;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(deltaPlane/2);
                optimizer.addEdge(e);

                vpEdgesNotSeenPlane.push_back(e);
                vpEdgeKFNotSeenPlane.push_back(pKFi);
                vpNotSeenMapPlane.push_back(pMP);
            }
        }

        const map<KeyFrame *, int> verObservations = pMP->GetVerObservations();
        for (map<KeyFrame *, int>::const_iterator mit = verObservations.begin(), mend = verObservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgeVerticalPlane *e = new g2o::EdgeVerticalPlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix2d Info;
                Info << verInfo, 0,
                        0, verInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(VPdeltaPlane);
                optimizer.addEdge(e);

                vpEdgesVerPlane.push_back(e);
            }
        }

        const map<KeyFrame *, int> parObservations = pMP->GetParObservations();
        for (map<KeyFrame *, int>::const_iterator mit = parObservations.begin(), mend = parObservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgeParallelPlane *e = new g2o::EdgeParallelPlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix2d Info;
                Info << parInfo, 0,
                        0, parInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(VPdeltaPlane);
                optimizer.addEdge(e);

                vpEdgesParPlane.push_back(e);
            }
        }
    }

    for (list<MapPlane *>::iterator lit = lLocalNotSeenMapPlanes.begin(), lend = lLocalNotSeenMapPlanes.end();
         lit != lend; lit++) {

        MapPlane *pMP = *lit;
        g2o::VertexPlane *vPlane = new g2o::VertexPlane();
        vPlane->setEstimate(Converter::toPlane3D(pMP->GetWorldPos()));
        int id = pMP->mnId + maxPointid + 1;
        vPlane->setId(id);
        vPlane->setMarginalized(true);
        optimizer.addVertex(vPlane);

        const map<KeyFrame *, int> observations = pMP->GetObservations();
        for (map<KeyFrame *, int>::const_iterator mit = observations.begin(), mend = observations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgePlane *e = new g2o::EdgePlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix3d Info;
                Info << angleInfo, 0, 0,
                        0, angleInfo, 0,
                        0, 0, disInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(deltaPlane);
                optimizer.addEdge(e);

                vpEdgesPlane.push_back(e);
                vpEdgeKFPlane.push_back(pKFi);
                vpMapPlane.push_back(pMP);
            }
        }

        const map<KeyFrame *, int> notseenobservations = pMP->GetNotSeenObservations();
        for (map<KeyFrame *, int>::const_iterator mit = notseenobservations.begin(), mend = notseenobservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgePlane *e = new g2o::EdgePlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvNotSeenPlaneCoefficients[mit->second]));
                Eigen::Matrix3d Info;
                Info << angleInfo/2, 0, 0,
                        0, angleInfo/2, 0,
                        0, 0, disInfo/2;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(deltaPlane/2);
                optimizer.addEdge(e);

                vpEdgesNotSeenPlane.push_back(e);
                vpEdgeKFNotSeenPlane.push_back(pKFi);
                vpNotSeenMapPlane.push_back(pMP);
            }
        }

        const map<KeyFrame *, int> verObservations = pMP->GetVerObservations();
        for (map<KeyFrame *, int>::const_iterator mit = verObservations.begin(), mend = verObservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgeVerticalPlane *e = new g2o::EdgeVerticalPlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix2d Info;
                Info << verInfo, 0,
                        0, verInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(VPdeltaPlane);
                optimizer.addEdge(e);

                vpEdgesVerPlane.push_back(e);
            }
        }

        const map<KeyFrame *, int> parObservations = pMP->GetParObservations();
        for (map<KeyFrame *, int>::const_iterator mit = parObservations.begin(), mend = parObservations.end();
             mit != mend; mit++) {
            KeyFrame *pKFi = mit->first;
            if (!pKFi->isBad()) {
                g2o::EdgeParallelPlane *e = new g2o::EdgeParallelPlane();
                if (optimizer.vertex(id) == NULL || optimizer.vertex(pKFi->mnId) == NULL)
                    continue;
                e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(id)));
                e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex *>(optimizer.vertex(pKFi->mnId)));
                e->setMeasurement(Converter::toPlane3D(pKFi->mvPlaneCoefficients[mit->second]));
                Eigen::Matrix2d Info;
                Info << parInfo, 0,
                        0, parInfo;
                e->setInformation(Info);
                g2o::RobustKernelHuber *rk = new g2o::RobustKernelHuber;
                e->setRobustKernel(rk);
                rk->setDelta(VPdeltaPlane);
                optimizer.addEdge(e);

                vpEdgesParPlane.push_back(e);
            }
        }
    }
    //--------------------------------------------------

    //--------------------------------------------------
    auto objects = pMap->GetAllMapObjects();

    if (use_objects == 1)
    {
        for (MapObject* pObj : objects) {
            ObjectTrack* tr = pObj->GetTrack();
            if (!tr)
                continue;
            for(list<KeyFrame*>::iterator lit=lLocalKeyFrames.begin(); lit!=lLocalKeyFrames.end(); lit++) {
                KeyFrame *kf = *lit;
                if(kf->isBad())
                    continue;

                auto [kf_bboxes,kf_scores] = pObj->GetTrack()->CopyDetectionsMapInKeyFrames();


                if (kf_bboxes.find(kf) != kf_bboxes.end()) {
                    Eigen::Matrix3d K = cvToEigenMatrix<double, float, 3, 3>(kf->mK);
                    auto bb = kf_bboxes[kf];
                    Ellipse ell = Ellipse::FromBbox(bb);
                    Ellipsoid ellipsoid = pObj->GetEllipsoid();


                    EdgeSE3ProjectEllipsoidOnlyPose *e = new EdgeSE3ProjectEllipsoidOnlyPose(ell, ellipsoid, K);
                    e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(kf->mnId)));
                    Eigen::Matrix<double, 1, 1> information_matrix = Eigen::Matrix<double, 1, 1>::Identity();
                    e->setInformation(information_matrix);
                    // std::cout << "Added edge with ellipsoid " << tr->GetId() << std::endl;
                    optimizer.addEdge(e);
                }
            }
        }
    }
    else if (use_objects == 2)
    {
        for (MapObject* pObj : objects) {
            ObjectTrack* tr = pObj->GetTrack();
            if (!tr)
                continue;

            VertexEllipsoidQuat* vertex = new VertexEllipsoidQuat();
            EllipsoidQuat ellipsoid_quat = EllipsoidQuat::FromEllipsoid(pObj->GetEllipsoid());
            vertex->setEstimate(ellipsoid_quat);

            // g2o::VertexSBAPointXYZ* vertex = new g2o::VertexSBAPointXYZ();
            // vertex->setEstimate(pObj->GetEllipsoid().GetCenter());


            int id = maxPointid + 1 + pObj->GetTrack()->GetId();
            vertex->setId(id);
            vertex->setMarginalized(true);
            optimizer.addVertex(vertex);


            auto [kf_bboxes,kf_scores] = pObj->GetTrack()->CopyDetectionsMapInKeyFrames();

            for(list<KeyFrame*>::iterator lit=lLocalKeyFrames.begin(); lit!=lLocalKeyFrames.end(); lit++) {
                KeyFrame *kf = *lit;
                if(kf->isBad())
                    continue;

                if (kf_bboxes.find(kf) != kf_bboxes.end()) {
                    Eigen::Matrix3d K = cvToEigenMatrix<double, float, 3, 3>(kf->mK);
                    auto bb = kf_bboxes[kf];
                    Ellipse ell = Ellipse::FromBbox(bb);
                    Ellipsoid ellipsoid = pObj->GetEllipsoid();

                    EdgeSE3ProjectEllipsoid *e = new EdgeSE3ProjectEllipsoid(ell, K);
                    // EdgeSE3ProjectEllipsoidCenter *e = new EdgeSE3ProjectEllipsoidCenter(ell, K);
                    e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id)));
                    e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(kf->mnId)));
                    Eigen::Matrix<double, 1, 1> information_matrix = Eigen::Matrix<double, 1, 1>::Identity();
                    // Eigen::Matrix<double, 2, 2> information_matrix = Eigen::Matrix<double, 2, 2>::Identity();
                    e->setInformation(information_matrix);
                    // std::cout << "Added edge with ellipsoid " << tr->GetId() << std::endl;
                    optimizer.addEdge(e);
                }
            }
        }
    }
    //--------------------------------------------------



    if(pbStopFlag)
        if(*pbStopFlag)
            return;

    optimizer.initializeOptimization();
    if (use_objects == 2)
        optimizer.optimize(20);
    else
        optimizer.optimize(5);


    bool bDoMore= true;

    if(pbStopFlag)
        if(*pbStopFlag)
            bDoMore = false;

    if(bDoMore)
    {

    // Check inlier observations
    for(size_t i=0, iend=vpEdgesMono.size(); i<iend;i++)
    {
        g2o::EdgeSE3ProjectXYZ* e = vpEdgesMono[i];
        MapPoint* pMP = vpMapPointEdgeMono[i];

        if(pMP->isBad())
            continue;

        if(e->chi2()>5.991 || !e->isDepthPositive())
        {
            e->setLevel(1);
        }

        e->setRobustKernel(0);
    }

    for(size_t i=0, iend=vpEdgesStereo.size(); i<iend;i++)
    {
        g2o::EdgeStereoSE3ProjectXYZ* e = vpEdgesStereo[i];
        MapPoint* pMP = vpMapPointEdgeStereo[i];

        if(pMP->isBad())
            continue;

        if(e->chi2()>7.815 || !e->isDepthPositive())
        {
            e->setLevel(1);
        }

        e->setRobustKernel(0);
    }

    for (size_t i = 0, iend = vpEdgesPlane.size(); i < iend; i++) {
        g2o::EdgePlane *e = vpEdgesPlane[i];

        if (e->chi2() > planeChi) {
            e->setLevel(1);
        }

        e->setRobustKernel(0);
    }

    for (size_t i = 0, iend = vpEdgesNotSeenPlane.size(); i < iend; i++) {
        g2o::EdgePlane *e = vpEdgesPlane[i];

        if (e->chi2() > planeChi/2) {
            e->setLevel(1);
        }

        e->setRobustKernel(0);
    }

    for (size_t i = 0, iend = vpEdgesParPlane.size(); i < iend; i++) {
        g2o::EdgeParallelPlane *e = vpEdgesParPlane[i];

        if (e->chi2() > VPplaneChi) {
            e->setLevel(1);
        }

        e->setRobustKernel(0);
    }

    for (size_t i = 0, iend = vpEdgesVerPlane.size(); i < iend; i++) {
        g2o::EdgeVerticalPlane *e = vpEdgesVerPlane[i];

        if (e->chi2() > VPplaneChi) {
            e->setLevel(1);
        }

        e->setRobustKernel(0);
    }
    // Optimize again without the outliers

    optimizer.initializeOptimization(0);
    optimizer.optimize(10);

    }

    vector<pair<KeyFrame*,MapPoint*> > vToErase;
    vToErase.reserve(vpEdgesMono.size()+vpEdgesStereo.size());

    vector<pair<KeyFrame*, MapPlane*> > vToErasePlane;
    vToErasePlane.reserve(vpEdgesPlane.size());

    vector<pair<KeyFrame*, MapPlane*> > vToEraseNotSeenPlane;
    vToEraseNotSeenPlane.reserve(vpEdgesNotSeenPlane.size());

    // Check inlier observations       
    for(size_t i=0, iend=vpEdgesMono.size(); i<iend;i++)
    {
        g2o::EdgeSE3ProjectXYZ* e = vpEdgesMono[i];
        MapPoint* pMP = vpMapPointEdgeMono[i];

        if(pMP->isBad())
            continue;

        if(e->chi2()>5.991 || !e->isDepthPositive())
        {
            KeyFrame* pKFi = vpEdgeKFMono[i];
            vToErase.push_back(make_pair(pKFi,pMP));
        }
    }

    for(size_t i=0, iend=vpEdgesStereo.size(); i<iend;i++)
    {
        g2o::EdgeStereoSE3ProjectXYZ* e = vpEdgesStereo[i];
        MapPoint* pMP = vpMapPointEdgeStereo[i];

        if(pMP->isBad())
            continue;

        if(e->chi2()>7.815 || !e->isDepthPositive())
        {
            KeyFrame* pKFi = vpEdgeKFStereo[i];
            vToErase.push_back(make_pair(pKFi,pMP));
        }
    }

    for(size_t i=0, iend=vpEdgesPlane.size(); i<iend;i++)
    {
        g2o::EdgePlane* e = vpEdgesPlane[i];

        if(e->chi2()>planeChi)
        {
            MapPlane* pMP = vpMapPlane[i];
            KeyFrame* pKFi = vpEdgeKFPlane[i];
            vToErasePlane.push_back(make_pair(pKFi, pMP));
        }

    }

    for(size_t i=0, iend=vpEdgesNotSeenPlane.size(); i<iend;i++)
    {
        g2o::EdgePlane* e = vpEdgesNotSeenPlane[i];

        if(e->chi2()>planeChi/2)
        {
            MapPlane* pMP = vpNotSeenMapPlane[i];
            KeyFrame* pKFi = vpEdgeKFNotSeenPlane[i];
            vToEraseNotSeenPlane.push_back(make_pair(pKFi, pMP));
        }

    }

    // Get Map Mutex
    unique_lock<mutex> lock(pMap->mMutexMapUpdate);

    if(!vToErase.empty())
    {
        for(size_t i=0;i<vToErase.size();i++)
        {
            KeyFrame* pKFi = vToErase[i].first;
            MapPoint* pMPi = vToErase[i].second;
            pKFi->EraseMapPointMatch(pMPi);
            pMPi->EraseObservation(pKFi);
        }
    }

//    if(!vToErasePlane.empty()){
//        for(size_t i=0;i<vToErasePlane.size();i++)
//        {
//            KeyFrame* pKFi = vToErasePlane[i].first;
//            MapPlane* pMPi = vToErasePlane[i].second;
//            pKFi->EraseMapPlaneMatch(pMPi);
//            pMPi->EraseObservation(pKFi);
//        }
//    }

    if(!vToEraseNotSeenPlane.empty()){
        for(size_t i=0;i<vToEraseNotSeenPlane.size();i++)
        {
            KeyFrame* pKFi = vToEraseNotSeenPlane[i].first;
            MapPlane* pMPi = vToEraseNotSeenPlane[i].second;
            pKFi->EraseNotSeenMapPlaneMatch(pMPi);
            pMPi->EraseNotSeenObservation(pKFi);
        }
    }
    // Recover optimized data

    //Keyframes
    for(list<KeyFrame*>::iterator lit=lLocalKeyFrames.begin(), lend=lLocalKeyFrames.end(); lit!=lend; lit++)
    {
        KeyFrame* pKF = *lit;
        g2o::VertexSE3Expmap* vSE3 = static_cast<g2o::VertexSE3Expmap*>(optimizer.vertex(pKF->mnId));
        g2o::SE3Quat SE3quat = vSE3->estimate();
        pKF->SetPose(Converter::toCvMat(SE3quat));
    }

    //Points
    for(list<MapPoint*>::iterator lit=lLocalMapPoints.begin(), lend=lLocalMapPoints.end(); lit!=lend; lit++)
    {
        MapPoint* pMP = *lit;
        g2o::VertexSBAPointXYZ* vPoint = static_cast<g2o::VertexSBAPointXYZ*>(optimizer.vertex(pMP->mnId+maxKFid+1));
        pMP->SetWorldPos(Converter::toCvMat(vPoint->estimate()));
        pMP->UpdateNormalAndDepth();
    }

    //Planes
    for(list<MapPlane*>::iterator lit=lLocalMapPlanes.begin(), lend=lLocalMapPlanes.end(); lit!=lend; lit++)
    {
        MapPlane* pMP = *lit;
        g2o::VertexPlane* vPlane = static_cast<g2o::VertexPlane*>(optimizer.vertex(pMP->mnId+maxPointid+1));
        pMP->SetWorldPos(Converter::toCvMat(vPlane->estimate()));
    }
    //Not Seen Planes
    for(list<MapPlane*>::iterator lit=lLocalNotSeenMapPlanes.begin(), lend=lLocalNotSeenMapPlanes.end(); lit!=lend; lit++)
    {
        MapPlane* pMP = *lit;
        g2o::VertexPlane* vPlane = static_cast<g2o::VertexPlane*>(optimizer.vertex(pMP->mnId+maxPointid+1));
        pMP->SetWorldPos(Converter::toCvMat(vPlane->estimate()));
    }

    // Ellipsoids
    if (use_objects == 2)
    {
        for (MapObject* pObj : objects)
        {
            ObjectTrack* tr = pObj->GetTrack();
            if (!tr)
                continue;

            int id = maxPointid + 1 + pObj->GetTrack()->GetId();
            VertexEllipsoidQuat* vEll = static_cast<VertexEllipsoidQuat*>(optimizer.vertex(id));
            EllipsoidQuat ell_quat_est = vEll->estimate();
            Ellipsoid new_ellipsoid = ell_quat_est.ToEllipsoid();
            pObj->SetEllipsoid(new_ellipsoid);

            // g2o::VertexSBAPointXYZ* vEll = static_cast<g2o::VertexSBAPointXYZ*>(optimizer.vertex(id));
            // Eigen::Vector3d pos = vEll->estimate();
            // auto ell = pObj->GetEllipsoid();
            // Ellipsoid new_ellipsoid(ell.GetAxes(), ell.GetOrientation(), pos);
            // pObj->SetEllipsoid(new_ellipsoid);
        }
    }

}


void Optimizer::OptimizeEssentialGraph(Map* pMap, KeyFrame* pLoopKF, KeyFrame* pCurKF,
                                       const LoopClosing::KeyFrameAndPose &NonCorrectedSim3,
                                       const LoopClosing::KeyFrameAndPose &CorrectedSim3,
                                       const map<KeyFrame *, set<KeyFrame *> > &LoopConnections, const bool &bFixScale)
{
    // Setup optimizer
    g2o::SparseOptimizer optimizer;
    optimizer.setVerbose(false);
    g2o::BlockSolver_7_3::LinearSolverType * linearSolver =
           new g2o::LinearSolverEigen<g2o::BlockSolver_7_3::PoseMatrixType>();
    g2o::BlockSolver_7_3 * solver_ptr= new g2o::BlockSolver_7_3(linearSolver);
    g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);

    solver->setUserLambdaInit(1e-16);
    optimizer.setAlgorithm(solver);

    const vector<KeyFrame*> vpKFs = pMap->GetAllKeyFrames();
    const vector<MapPoint*> vpMPs = pMap->GetAllMapPoints();

    const unsigned int nMaxKFid = pMap->GetMaxKFid();

    vector<g2o::Sim3,Eigen::aligned_allocator<g2o::Sim3> > vScw(nMaxKFid+1);
    vector<g2o::Sim3,Eigen::aligned_allocator<g2o::Sim3> > vCorrectedSwc(nMaxKFid+1);
    vector<g2o::VertexSim3Expmap*> vpVertices(nMaxKFid+1);

    const int minFeat = 100;

    // Set KeyFrame vertices
    for(size_t i=0, iend=vpKFs.size(); i<iend;i++)
    {
        KeyFrame* pKF = vpKFs[i];
        if(pKF->isBad())
            continue;
        g2o::VertexSim3Expmap* VSim3 = new g2o::VertexSim3Expmap();

        const int nIDi = pKF->mnId;

        LoopClosing::KeyFrameAndPose::const_iterator it = CorrectedSim3.find(pKF);

        if(it!=CorrectedSim3.end())
        {
            vScw[nIDi] = it->second;
            VSim3->setEstimate(it->second);
        }
        else
        {
            Eigen::Matrix<double,3,3> Rcw = Converter::toMatrix3d(pKF->GetRotation());
            Eigen::Matrix<double,3,1> tcw = Converter::toVector3d(pKF->GetTranslation());
            g2o::Sim3 Siw(Rcw,tcw,1.0);
            vScw[nIDi] = Siw;
            VSim3->setEstimate(Siw);
        }

        if(pKF==pLoopKF)
            VSim3->setFixed(true);

        VSim3->setId(nIDi);
        VSim3->setMarginalized(false);
        VSim3->_fix_scale = bFixScale;

        optimizer.addVertex(VSim3);

        vpVertices[nIDi]=VSim3;
    }


    set<pair<long unsigned int,long unsigned int> > sInsertedEdges;

    const Eigen::Matrix<double,7,7> matLambda = Eigen::Matrix<double,7,7>::Identity();

    // Set Loop edges
    for(map<KeyFrame *, set<KeyFrame *> >::const_iterator mit = LoopConnections.begin(), mend=LoopConnections.end(); mit!=mend; mit++)
    {
        KeyFrame* pKF = mit->first;
        const long unsigned int nIDi = pKF->mnId;
        const set<KeyFrame*> &spConnections = mit->second;
        const g2o::Sim3 Siw = vScw[nIDi];
        const g2o::Sim3 Swi = Siw.inverse();

        for(set<KeyFrame*>::const_iterator sit=spConnections.begin(), send=spConnections.end(); sit!=send; sit++)
        {
            const long unsigned int nIDj = (*sit)->mnId;
            if((nIDi!=pCurKF->mnId || nIDj!=pLoopKF->mnId) && pKF->GetWeight(*sit)<minFeat)
                continue;

            const g2o::Sim3 Sjw = vScw[nIDj];
            const g2o::Sim3 Sji = Sjw * Swi;

            g2o::EdgeSim3* e = new g2o::EdgeSim3();
            e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(nIDj)));
            e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(nIDi)));
            e->setMeasurement(Sji);

            e->information() = matLambda;

            optimizer.addEdge(e);

            sInsertedEdges.insert(make_pair(min(nIDi,nIDj),max(nIDi,nIDj)));
        }
    }

    // Set normal edges
    for(size_t i=0, iend=vpKFs.size(); i<iend; i++)
    {
        KeyFrame* pKF = vpKFs[i];

        const int nIDi = pKF->mnId;

        g2o::Sim3 Swi;

        LoopClosing::KeyFrameAndPose::const_iterator iti = NonCorrectedSim3.find(pKF);

        if(iti!=NonCorrectedSim3.end())
            Swi = (iti->second).inverse();
        else
            Swi = vScw[nIDi].inverse();

        KeyFrame* pParentKF = pKF->GetParent();

        // Spanning tree edge
        if(pParentKF)
        {
            int nIDj = pParentKF->mnId;

            g2o::Sim3 Sjw;

            LoopClosing::KeyFrameAndPose::const_iterator itj = NonCorrectedSim3.find(pParentKF);

            if(itj!=NonCorrectedSim3.end())
                Sjw = itj->second;
            else
                Sjw = vScw[nIDj];

            g2o::Sim3 Sji = Sjw * Swi;

            g2o::EdgeSim3* e = new g2o::EdgeSim3();
            e->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(nIDj)));
            e->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(nIDi)));
            e->setMeasurement(Sji);

            e->information() = matLambda;
            optimizer.addEdge(e);
        }

        // Loop edges
        const set<KeyFrame*> sLoopEdges = pKF->GetLoopEdges();
        for(set<KeyFrame*>::const_iterator sit=sLoopEdges.begin(), send=sLoopEdges.end(); sit!=send; sit++)
        {
            KeyFrame* pLKF = *sit;
            if(pLKF->mnId<pKF->mnId)
            {
                g2o::Sim3 Slw;

                LoopClosing::KeyFrameAndPose::const_iterator itl = NonCorrectedSim3.find(pLKF);

                if(itl!=NonCorrectedSim3.end())
                    Slw = itl->second;
                else
                    Slw = vScw[pLKF->mnId];

                g2o::Sim3 Sli = Slw * Swi;
                g2o::EdgeSim3* el = new g2o::EdgeSim3();
                el->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(pLKF->mnId)));
                el->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(nIDi)));
                el->setMeasurement(Sli);
                el->information() = matLambda;
                optimizer.addEdge(el);
            }
        }

        // Covisibility graph edges
        const vector<KeyFrame*> vpConnectedKFs = pKF->GetCovisiblesByWeight(minFeat);
        for(vector<KeyFrame*>::const_iterator vit=vpConnectedKFs.begin(); vit!=vpConnectedKFs.end(); vit++)
        {
            KeyFrame* pKFn = *vit;
            if(pKFn && pKFn!=pParentKF && !pKF->hasChild(pKFn) && !sLoopEdges.count(pKFn))
            {
                if(!pKFn->isBad() && pKFn->mnId<pKF->mnId)
                {
                    if(sInsertedEdges.count(make_pair(min(pKF->mnId,pKFn->mnId),max(pKF->mnId,pKFn->mnId))))
                        continue;

                    g2o::Sim3 Snw;

                    LoopClosing::KeyFrameAndPose::const_iterator itn = NonCorrectedSim3.find(pKFn);

                    if(itn!=NonCorrectedSim3.end())
                        Snw = itn->second;
                    else
                        Snw = vScw[pKFn->mnId];

                    g2o::Sim3 Sni = Snw * Swi;

                    g2o::EdgeSim3* en = new g2o::EdgeSim3();
                    en->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(pKFn->mnId)));
                    en->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(nIDi)));
                    en->setMeasurement(Sni);
                    en->information() = matLambda;
                    optimizer.addEdge(en);
                }
            }
        }
    }

    // Optimize!
    optimizer.initializeOptimization();
    optimizer.optimize(20);

    unique_lock<mutex> lock(pMap->mMutexMapUpdate);

    // SE3 Pose Recovering. Sim3:[sR t;0 1] -> SE3:[R t/s;0 1]
    for(size_t i=0;i<vpKFs.size();i++)
    {
        KeyFrame* pKFi = vpKFs[i];

        const int nIDi = pKFi->mnId;

        g2o::VertexSim3Expmap* VSim3 = static_cast<g2o::VertexSim3Expmap*>(optimizer.vertex(nIDi));
        g2o::Sim3 CorrectedSiw =  VSim3->estimate();
        vCorrectedSwc[nIDi]=CorrectedSiw.inverse();
        Eigen::Matrix3d eigR = CorrectedSiw.rotation().toRotationMatrix();
        Eigen::Vector3d eigt = CorrectedSiw.translation();
        double s = CorrectedSiw.scale();

        eigt *=(1./s); //[R t/s;0 1]

        cv::Mat Tiw = Converter::toCvSE3(eigR,eigt);

        pKFi->SetPose(Tiw);
    }

    // Correct points. Transform to "non-optimized" reference keyframe pose and transform back with optimized pose
    for(size_t i=0, iend=vpMPs.size(); i<iend; i++)
    {
        MapPoint* pMP = vpMPs[i];

        if(pMP->isBad())
            continue;

        int nIDr;
        if(pMP->mnCorrectedByKF==pCurKF->mnId)
        {
            nIDr = pMP->mnCorrectedReference;
        }
        else
        {
            KeyFrame* pRefKF = pMP->GetReferenceKeyFrame();
            nIDr = pRefKF->mnId;
        }


        g2o::Sim3 Srw = vScw[nIDr];
        g2o::Sim3 correctedSwr = vCorrectedSwc[nIDr];

        cv::Mat P3Dw = pMP->GetWorldPos();
        Eigen::Matrix<double,3,1> eigP3Dw = Converter::toVector3d(P3Dw);
        Eigen::Matrix<double,3,1> eigCorrectedP3Dw = correctedSwr.map(Srw.map(eigP3Dw));

        cv::Mat cvCorrectedP3Dw = Converter::toCvMat(eigCorrectedP3Dw);
        pMP->SetWorldPos(cvCorrectedP3Dw);

        pMP->UpdateNormalAndDepth();
    }
}

int Optimizer::OptimizeSim3(KeyFrame *pKF1, KeyFrame *pKF2, vector<MapPoint *> &vpMatches1, g2o::Sim3 &g2oS12, const float th2, const bool bFixScale)
{
    g2o::SparseOptimizer optimizer;
    g2o::BlockSolverX::LinearSolverType * linearSolver;

    linearSolver = new g2o::LinearSolverDense<g2o::BlockSolverX::PoseMatrixType>();

    g2o::BlockSolverX * solver_ptr = new g2o::BlockSolverX(linearSolver);

    g2o::OptimizationAlgorithmLevenberg* solver = new g2o::OptimizationAlgorithmLevenberg(solver_ptr);
    optimizer.setAlgorithm(solver);

    // Calibration
    const cv::Mat &K1 = pKF1->mK;
    const cv::Mat &K2 = pKF2->mK;

    // Camera poses
    const cv::Mat R1w = pKF1->GetRotation();
    const cv::Mat t1w = pKF1->GetTranslation();
    const cv::Mat R2w = pKF2->GetRotation();
    const cv::Mat t2w = pKF2->GetTranslation();

    // Set Sim3 vertex
    g2o::VertexSim3Expmap * vSim3 = new g2o::VertexSim3Expmap();    
    vSim3->_fix_scale=bFixScale;
    vSim3->setEstimate(g2oS12);
    vSim3->setId(0);
    vSim3->setFixed(false);
    vSim3->_principle_point1[0] = K1.at<float>(0,2);
    vSim3->_principle_point1[1] = K1.at<float>(1,2);
    vSim3->_focal_length1[0] = K1.at<float>(0,0);
    vSim3->_focal_length1[1] = K1.at<float>(1,1);
    vSim3->_principle_point2[0] = K2.at<float>(0,2);
    vSim3->_principle_point2[1] = K2.at<float>(1,2);
    vSim3->_focal_length2[0] = K2.at<float>(0,0);
    vSim3->_focal_length2[1] = K2.at<float>(1,1);
    optimizer.addVertex(vSim3);

    // Set MapPoint vertices
    const int N = vpMatches1.size();
    const vector<MapPoint*> vpMapPoints1 = pKF1->GetMapPointMatches();
    vector<g2o::EdgeSim3ProjectXYZ*> vpEdges12;
    vector<g2o::EdgeInverseSim3ProjectXYZ*> vpEdges21;
    vector<size_t> vnIndexEdge;

    vnIndexEdge.reserve(2*N);
    vpEdges12.reserve(2*N);
    vpEdges21.reserve(2*N);

    const float deltaHuber = sqrt(th2);

    int nCorrespondences = 0;

    for(int i=0; i<N; i++)
    {
        if(!vpMatches1[i])
            continue;

        MapPoint* pMP1 = vpMapPoints1[i];
        MapPoint* pMP2 = vpMatches1[i];

        const int id1 = 2*i+1;
        const int id2 = 2*(i+1);

        const int i2 = pMP2->GetIndexInKeyFrame(pKF2);

        if(pMP1 && pMP2)
        {
            if(!pMP1->isBad() && !pMP2->isBad() && i2>=0)
            {
                g2o::VertexSBAPointXYZ* vPoint1 = new g2o::VertexSBAPointXYZ();
                cv::Mat P3D1w = pMP1->GetWorldPos();
                cv::Mat P3D1c = R1w*P3D1w + t1w;
                vPoint1->setEstimate(Converter::toVector3d(P3D1c));
                vPoint1->setId(id1);
                vPoint1->setFixed(true);
                optimizer.addVertex(vPoint1);

                g2o::VertexSBAPointXYZ* vPoint2 = new g2o::VertexSBAPointXYZ();
                cv::Mat P3D2w = pMP2->GetWorldPos();
                cv::Mat P3D2c = R2w*P3D2w + t2w;
                vPoint2->setEstimate(Converter::toVector3d(P3D2c));
                vPoint2->setId(id2);
                vPoint2->setFixed(true);
                optimizer.addVertex(vPoint2);
            }
            else
                continue;
        }
        else
            continue;

        nCorrespondences++;

        // Set edge x1 = S12*X2
        Eigen::Matrix<double,2,1> obs1;
        const cv::KeyPoint &kpUn1 = pKF1->mvKeysUn[i];
        obs1 << kpUn1.pt.x, kpUn1.pt.y;

        g2o::EdgeSim3ProjectXYZ* e12 = new g2o::EdgeSim3ProjectXYZ();
        e12->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id2)));
        e12->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(0)));
        e12->setMeasurement(obs1);
        const float &invSigmaSquare1 = pKF1->mvInvLevelSigma2[kpUn1.octave];
        e12->setInformation(Eigen::Matrix2d::Identity()*invSigmaSquare1);

        g2o::RobustKernelHuber* rk1 = new g2o::RobustKernelHuber;
        e12->setRobustKernel(rk1);
        rk1->setDelta(deltaHuber);
        optimizer.addEdge(e12);

        // Set edge x2 = S21*X1
        Eigen::Matrix<double,2,1> obs2;
        const cv::KeyPoint &kpUn2 = pKF2->mvKeysUn[i2];
        obs2 << kpUn2.pt.x, kpUn2.pt.y;

        g2o::EdgeInverseSim3ProjectXYZ* e21 = new g2o::EdgeInverseSim3ProjectXYZ();

        e21->setVertex(0, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(id1)));
        e21->setVertex(1, dynamic_cast<g2o::OptimizableGraph::Vertex*>(optimizer.vertex(0)));
        e21->setMeasurement(obs2);
        float invSigmaSquare2 = pKF2->mvInvLevelSigma2[kpUn2.octave];
        e21->setInformation(Eigen::Matrix2d::Identity()*invSigmaSquare2);

        g2o::RobustKernelHuber* rk2 = new g2o::RobustKernelHuber;
        e21->setRobustKernel(rk2);
        rk2->setDelta(deltaHuber);
        optimizer.addEdge(e21);

        vpEdges12.push_back(e12);
        vpEdges21.push_back(e21);
        vnIndexEdge.push_back(i);
    }

    // Optimize!
    optimizer.initializeOptimization();
    optimizer.optimize(5);

    // Check inliers
    int nBad=0;
    for(size_t i=0; i<vpEdges12.size();i++)
    {
        g2o::EdgeSim3ProjectXYZ* e12 = vpEdges12[i];
        g2o::EdgeInverseSim3ProjectXYZ* e21 = vpEdges21[i];
        if(!e12 || !e21)
            continue;

        if(e12->chi2()>th2 || e21->chi2()>th2)
        {
            size_t idx = vnIndexEdge[i];
            vpMatches1[idx]=static_cast<MapPoint*>(NULL);
            optimizer.removeEdge(e12);
            optimizer.removeEdge(e21);
            vpEdges12[i]=static_cast<g2o::EdgeSim3ProjectXYZ*>(NULL);
            vpEdges21[i]=static_cast<g2o::EdgeInverseSim3ProjectXYZ*>(NULL);
            nBad++;
        }
    }

    int nMoreIterations;
    if(nBad>0)
        nMoreIterations=10;
    else
        nMoreIterations=5;

    if(nCorrespondences-nBad<10)
        return 0;

    // Optimize again only with inliers

    optimizer.initializeOptimization();
    optimizer.optimize(nMoreIterations);

    int nIn = 0;
    for(size_t i=0; i<vpEdges12.size();i++)
    {
        g2o::EdgeSim3ProjectXYZ* e12 = vpEdges12[i];
        g2o::EdgeInverseSim3ProjectXYZ* e21 = vpEdges21[i];
        if(!e12 || !e21)
            continue;

        if(e12->chi2()>th2 || e21->chi2()>th2)
        {
            size_t idx = vnIndexEdge[i];
            vpMatches1[idx]=static_cast<MapPoint*>(NULL);
        }
        else
            nIn++;
    }

    // Recover optimized Sim3
    g2o::VertexSim3Expmap* vSim3_recov = static_cast<g2o::VertexSim3Expmap*>(optimizer.vertex(0));
    g2oS12= vSim3_recov->estimate();

    return nIn;
}


} //namespace ORB_SLAM
