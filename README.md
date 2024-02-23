# CrossArmFusion

Densedepthestimationissignificantinroboticsystems,suchasformapping,localization,andobjectrecognition.Formultiple sensors, an active depth sensor can provide accurate but sparse measurements for environments, and a camera pair can provide dense but imprecise stereo reconstruction results. 

In this project, a tightly coupled fusion method is proposed for depth sensor and stereo camera to complete dense depth estimation, and advantages of the two type sensors are combined so as to achieve better depth estimation. An adaptive dynamic cross-arm algorithm are developed to integrate sparse depth measurements into camera-dominated semiglobal stereo matching. 

To obtain the optimal arm length for a measured pixel point, each cross-arm shape is variational and calculated automatically. Public datasets of KITTI, Middlebury, and Scene Flow datasets are used with comparison experiments to test performance of the proposed method, and real-world experiments are further conducted for verification. 

For more technical details, please refer to:

H. Mo, B. Li, W. Shi, and X. Zhang, ``Cross-based dense depth estimation by fusing stereo vision with measured sparse depth", The Visual Computer, vol. 39, pp. 4339–4350, Sep. 2023.
