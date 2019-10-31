/*
 * Copyright 2012-2019 CNRS-UM LIRMM, CNRS-AIST JRL
 */

// includes
// std
#include <iostream>
#include <vector>

// boost
#define BOOST_TEST_MODULE JointTest
#include <boost/math/constants/constants.hpp>
#include <boost/test/unit_test.hpp>

// SpaceVecAlg
#include <SpaceVecAlg/SpaceVecAlg>

// RBDyn
#include "RBDyn/Body.h"
#include "RBDyn/EulerIntegration.h"
#include "RBDyn/FK.h"
#include "RBDyn/FV.h"
#include "RBDyn/Jacobian.h"
#include "RBDyn/Joint.h"
#include "RBDyn/MultiBody.h"
#include "RBDyn/MultiBodyConfig.h"
#include "RBDyn/MultiBodyGraph.h"

using namespace Eigen;
using namespace sva;
using namespace rbd;
namespace constants = boost::math::constants;

/** Timesteps used in the tests*/
const std::vector<double> dt = {0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1};
/** Joints tested*/
const std::vector<Joint::Type> types = {Joint::Rev,    Joint::Prism,       Joint::Spherical,
                                       /*Joint::Planar,*/ Joint::Cylindrical, Joint::Free};
// const std::vector<Joint::Type> types = {Joint::Planar};
// const std::vector<Joint::Type> types = {Joint::Free};

/// @return A robot with a single joint specified by the user
std::tuple<rbd::MultiBody, rbd::MultiBodyConfig, rbd::MultiBodyGraph> makeSingleJointRobot(
    Joint::Type type,
    const Vector3d & axis = Vector3d::UnitZ(),
    const Vector3d & endEffector = Vector3d::Zero())
{
  MultiBodyGraph mbg;

  double mass = 1.;
  Matrix3d I = Matrix3d::Identity();
  Vector3d h = Vector3d::Zero();
  RBInertiad rbi(mass, h, I);

  Body b0(rbi, "b0");
  Body b1(rbi, "b1");
  mbg.addBody(b0);
  mbg.addBody(b1);

  Joint j0(type, axis, true, "j0");
  mbg.addJoint(j0);

  PTransformd to(Vector3d(0., 0., 0.));
  PTransformd from(Vector3d(0., 0., 0.));
  mbg.linkBodies("b0", to, "b1", from, "j0");

  if(!endEffector.isZero())
  {
    Body ee(rbi, "ee");
    mbg.addBody(ee);
    Joint je(Joint::Fixed, true, "je");
    mbg.addJoint(je);
    mbg.linkBodies("b1", endEffector, "ee", from, "je");
  }

  MultiBody mb = mbg.makeMultiBody("b0", true);
  MultiBodyConfig mbc(mb);
  mbc.zero(mb);

  return std::make_tuple(mb, mbc, mbg);
}

std::vector<double> randVec(int size, double rmin, double rmax, bool normed = false)
{
  std::vector<double> v(size, 0);
  Map<VectorXd> r(&v[0], size, 1);
  r = (rmax - rmin) * VectorXd::Random(size).cwiseAbs() + VectorXd::Constant(size, rmin);

  if(normed) r.normalize();

  return v;
}

std::tuple<std::vector<double>, std::vector<double>, std::vector<double>> randQVA(Joint::Type type)
{
  std::vector<double> q, v, a;
  switch(type)
  {
    case Joint::Rev:
    case Joint::Prism:
      q = randVec(1, -1, 1);
      v = randVec(1, -1, 1);
      a = randVec(1, -1, 1);
      break;
    case Joint::Spherical:
      q = randVec(4, -1, 1, true);
      v = randVec(3, -1, 1);
      a = randVec(3, -1, 1);
      break;
    case Joint::Planar:
      q = randVec(3, -1, 1);
      v = randVec(3, -1, 1);
      a = randVec(3, -1, 1);
      break;
    case Joint::Cylindrical:
      q = randVec(2, -1, 1);
      v = randVec(2, -1, 1);
      a = randVec(2, -1, 1);
      break;
    case Joint::Free:
    {
      q = randVec(4, -1, 1, true);
      auto qt = randVec(3, -1, 1);
      q.insert(q.end(), qt.begin(), qt.end());
      v = randVec(6, -1, 1);
      a = randVec(6, -1, 1);
      break;
    }
    default:
      break;
  }

  return std::make_tuple(q, v, a);
}

/** Exact integration of a constant velocity \a vel over a time interval of size \a step
 * starting from position \a q.
 */
std::vector<double> explicitIntegrationAtConstantSpeed(Joint::Type type,
                                                       double step,
                                                       const std::vector<double> & q,
                                                       const std::vector<double> & vel)
{
  switch(type)
  {
    case Joint::Rev:
    case Joint::Prism:
      return {q[0] + vel[0] * step};
    case Joint::Spherical:
    {
      Quaterniond qi(q[0], q[1], q[2], q[3]);
      Vector3d w(vel[0], vel[1], vel[2]);
      w *= step;
      double n = w.norm();
      double s = sva::sinc(n / 2) / 2;
      Quaterniond qexp(std::cos(n / 2), s * w.x(), s * w.y(), s * w.z());
      Quaterniond qf = qi * qexp;
      return {qf.w(), qf.x(), qf.y(), qf.z()};
    }
    case Joint::Planar:
    {
       double tw = step * vel[0];
       double c = std::cos(tw);
       double s = std::sin(tw);
       double sc = sva::sinc(tw);
       double cc;
       if(std::abs(tw) > 1e-4)
        cc = (1 - c) / tw;
       else
        cc = tw / 2 * (1 - tw * tw / 12);
       double q1step = sc * vel[1] + cc * vel[2];
       double q2step = -cc * vel[1] + sc * vel[2];
       return { q[0] + tw,
               c * q[1] + s * q[2] + q1step * step,
              -s * q[1] + c * q[2] + q2step * step };
      //double c0 = std::cos(q[0]);
      //double s0 = std::sin(q[0]);
      //double ct = std::cos(q[0] + tw);
      //double st = std::sin(q[0] + tw);
      //Matrix2d R0;
      //R0 << c0, -s0, s0, c0;
      //Matrix2d Rt;
      //Rt << ct, -st, st, ct;
      //Map<const Vector2d> p0(&q[1]);
      //Map<const Vector2d> v(&vel[1]);
      //Vector2d pt = Rt * (R0.transpose() * p0 + v * step);
      //return {q[0] + tw, pt.x(), pt.y()};
      // Vector2d p(c0 * q[1] + s0 * q[2] + vel[1] * step, -s0 * q[1] + c0 * q[2] + vel[2] * step);
      // return {q[0] + tw, ct * p.x() - st * p.y(), st * p.x() + ct * p.y()};
      // return {q[0] + tw,
      //        q[1] + (-vel[0] * q[2] + c0 * vel[1] - s0 * vel[2]) * step,
      //        q[2] + (vel[0] * q[1] + s0 * q[1] + c0 * q[2]) * step};
    }
    case Joint::Cylindrical:
      return {q[0] + vel[0] * step, q[1] + vel[1] * step};
    case Joint::Free:
    {
      // integration of the orientation part
      Quaterniond qi(q[0], q[1], q[2], q[3]);
      Vector3d w(vel[0], vel[1], vel[2]);
      Vector3d tw = step * w;
      double n = w.norm();
      double tn = step * n;
      double s = sva::sinc(tn / 2) / 2;
      Quaterniond qexp(std::cos(tn / 2), s * tw.x(), s * tw.y(), s * tw.z());
      Quaterniond qf = qi * qexp;

      // integration of the position part: we need to integrate R(t)v
      // using Rodrigues formula, we have that the primitive of exp(t hat(w)) is
      // E(t) = (t I - cos(t ||w||)/||w||^2 hat(w) + (t - sin(t ||w||)/||w||^2)hat(w)^2)
      // We now compute xi + qi*(E(t)-E(0))*v
      Vector3d x(q[4], q[5], q[6]);
      Vector3d v(vel[3], vel[4], vel[5]);
      Vector3d wv = w.cross(v);
      Vector3d w2v = w.cross(wv);
      double a, b;
      if(tn > 1e-4)
        a = (1 - std::cos(tn)) / (n * n);
      else
        a = tn * tn / 2;
      b = step * (1 - sva::sinc(tn)) / (n * n);
      Vector3d dx = step * v + a * wv + b * w2v;
      Vector3d xf = x + qi * dx;
      return {qf.w(), qf.x(), qf.y(), qf.z(), xf.x(), xf.y(), xf.z()};
    }
  }
}

void testConstantSpeedIntegration(Joint::Type type,
                                  double step,
                                  const std::vector<double> & q,
                                  const std::vector<double> & v)
{
  MultiBody mb;
  MultiBodyConfig mbc;
  MultiBodyGraph mbg;
  std::tie(mb, mbc, mbg) = makeSingleJointRobot(type);

  mbc.q = {{}, q};
  mbc.alpha = {{}, v};
  mbc.alphaD = {{}, std::vector<double>(v.size(), 0)};
  forwardKinematics(mb, mbc);

  eulerIntegration(mb, mbc, step);

  auto q_expected2 = explicitIntegrationAtConstantSpeed(type, step, q, v);
  std::cout << "for type = " << type << ", with dt=" << step << std::endl;
  std::cout << "expected = ";
  for(size_t i = 0; i < q.size(); ++i) std::cout << q_expected2[i] << " ";
  std::cout << std::endl;
  std::cout << "computed = ";
  for(size_t i = 0; i < q.size(); ++i) std::cout << mbc.q[1][i] << " ";
  std::cout << std::endl;
  std::cout << "error    = ";
  for(size_t i = 0; i < q.size(); ++i) std::cout << std::abs(mbc.q[1][i] - q_expected2[i]) << " ";
  std::cout << std::endl;

  for(size_t i = 0; i < q.size(); ++i)
  {
    BOOST_CHECK_CLOSE_FRACTION(q_expected2[i], mbc.q[1][i], 1e-8);
  }
}

// This tests the consistency of the integration, i.e. that intergrating one time over
// step gives the same result as integrating N time over step/N
void testIntegrationConsistency(Joint::Type type,
                                double step,
                                const std::vector<double> & q,
                                const std::vector<double> & v,
                                const std::vector<double> & a)
{
  MultiBody mb;
  MultiBodyConfig mbc;
  MultiBodyGraph mbg;
  std::tie(mb, mbc, mbg) = makeSingleJointRobot(type);
  MultiBodyConfig mbc0(mbc);

  mbc.q = {{}, q};
  mbc.alpha = {{}, v};
  mbc.alphaD = {{}, a};
  mbc0.q = {{}, q};
  mbc0.alpha = {{}, v};
  mbc0.alphaD = {{}, a};

  forwardKinematics(mb, mbc);
  forwardKinematics(mb, mbc0);

  // integrating on the whole time step
  eulerIntegration(mb, mbc0, step);

  // integrating with on small time step
  const int N = 2000;
  for(int i = 0; i < N; ++i)
  {
    eulerIntegration(mb, mbc, step / N);
    forwardKinematics(mb, mbc);
  }

  std::cout << "for type = " << type << ", with dt=" << step << std::endl;
  std::cout << "whole step  = ";
  for(size_t i = 0; i < q.size(); ++i) std::cout << mbc0.q[1][i] << " ";
  std::cout << std::endl;
  std::cout << "fractionned = ";
  for(size_t i = 0; i < q.size(); ++i) std::cout << mbc.q[1][i] << " ";
  std::cout << std::endl;
  std::cout << "error       = ";
  for(size_t i = 0; i < q.size(); ++i) std::cout << std::abs(mbc.q[1][i] - mbc0.q[1][i]) << " ";
  std::cout << std::endl;
  for(size_t i = 0; i < q.size(); ++i)
  {
    BOOST_CHECK_CLOSE_FRACTION(mbc0.q[1][i], mbc.q[1][i], 1e-4);
  }
}

void testConstantAccelerationIntegration(Joint::Type type,
                                         double step,
                                         const std::vector<double> & q,
                                         const std::vector<double> & v,
                                         const std::vector<double> & a)
{
  std::vector<double> qc = q;
  std::vector<double> vc = v;

  MultiBody mb;
  MultiBodyConfig mbc;
  MultiBodyGraph mbg;
  std::tie(mb, mbc, mbg) = makeSingleJointRobot(type);

  mbc.q = {{}, q};
  mbc.alpha = {{}, v};
  mbc.alphaD = {{}, a};

  forwardKinematics(mb, mbc);

  // integrating on the whole time step
  eulerIntegration(mb, mbc, step);

  // integrating with constant velocity on small time step
  const int N = 10000;
  double dt = step / N;
  // we take the speed at the middle of the interval
  for(size_t i = 0; i < vc.size(); ++i) vc[i] = vc[i] + a[i] * dt / 2;
  for(int i = 0; i < N; ++i)
  {
    qc = explicitIntegrationAtConstantSpeed(type, dt, qc, vc);
    for(size_t i = 0; i < vc.size(); ++i) vc[i] = vc[i] + a[i] * dt;
  }

  std::cout << "for type = " << type << ", with dt=" << step << std::endl;
  std::cout << "computed     = ";
  for(size_t i = 0; i < q.size(); ++i) std::cout << mbc.q[1][i] << " ";
  std::cout << std::endl;
  std::cout << "approximated = ";
  for(size_t i = 0; i < q.size(); ++i) std::cout << qc[i] << " ";
  std::cout << std::endl;
  std::cout << "error        = ";
  for(size_t i = 0; i < q.size(); ++i) std::cout << std::abs(mbc.q[1][i] - qc[i]) << " ";
  std::cout << std::endl;
  for(size_t i = 0; i < q.size(); ++i)
  {
    BOOST_CHECK_CLOSE_FRACTION(qc[i], mbc.q[1][i], 1e-4);
  }
}

void testConsistencyWithJacobian(Joint::Type type, const std::vector<double> & q, const std::vector<double> & v)
{
  double dt = 1;

  MultiBody mb;
  MultiBodyConfig mbc;
  MultiBodyGraph mbg;
  std::tie(mb, mbc, mbg) = makeSingleJointRobot(type, Vector3d::UnitZ(), Eigen::Vector3d(1, 1, 1));

  Jacobian J(mb, "ee");

  mbc.q = {{}, q, {}};
  mbc.alpha = {{}, v, {}};
  forwardKinematics(mb, mbc);
  forwardVelocity(mb, mbc);

  // Compute via Simpson's rule an approximate integration of J(q)v over dt
  // and add it to the initial pose of the end effector
  const int N = 100;
  std::vector<double> q1;
  std::vector<double> q2;
  // Initial position of the end effector
  Vector3d pi = mbc.bodyPosW[2].translation();
  Vector3d vi = J.velocity(mb, mbc).linear();
  double delta = dt / N;
  for(int i = 0; i < N / 2; ++i)
  {
    q1 = explicitIntegrationAtConstantSpeed(type, (2 * i + 1) * delta, q, v);
    mbc.q = {{}, q1, {}};
    forwardKinematics(mb, mbc);
    forwardVelocity(mb, mbc);
    Vector3d v1 = J.velocity(mb, mbc).linear();
    q2 = explicitIntegrationAtConstantSpeed(type, (2 * i + 2) * delta, q, v);
    mbc.q = {{}, q2, {}};
    forwardKinematics(mb, mbc);
    forwardVelocity(mb, mbc);
    Vector3d v2 = J.velocity(mb, mbc).linear();
    pi += delta / 3 * (vi + 4 * v1 + v2);
    vi = v2;
  }

  // Compute the integration in one step of the joint velocity
  std::vector<double> qf = explicitIntegrationAtConstantSpeed(type, dt, q, v);
  mbc.q = {{}, qf, {}};
  forwardKinematics(mb, mbc);
  // Compute the position of the end effector at this configuration
  Vector3d pe = mbc.bodyPosW[2].translation();

  std::cout << "For type = " << type << std::endl;
  std::cout << "expected = " << pi.transpose() << std::endl;
  std::cout << "computed = " << pe.transpose() << std::endl;
  std::cout << "error    = " << (pi - pe).cwiseAbs().transpose() << std::endl;

  BOOST_CHECK_SMALL((pi - pe).norm(), 1e-8);
}

BOOST_AUTO_TEST_CASE(ConstantSpeedJointIntegrationTest)
{
  std::cout << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n" << std::endl;

  for(auto step : dt)
  {
    for(auto type : types)
    {
      std::vector<double> q, v, a;
      std::tie(q, v, a) = randQVA(type);
      testConstantSpeedIntegration(type, step, q, v);
    }
  }
}

BOOST_AUTO_TEST_CASE(IntegrationConsistencyTest)
{
  std::cout << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n" << std::endl;

  for(auto step : dt)
  {
    for(auto type : types)
    {
      std::vector<double> q, v, a;
      std::tie(q, v, a) = randQVA(type);
      testIntegrationConsistency(type, step, q, v, a);
    }
  }
}

BOOST_AUTO_TEST_CASE(ConstantAccelerationJointIntegrationTest)
{
  std::cout << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n" << std::endl;

  for(auto step : dt)
  {
    for(auto type : types)
    {
      std::vector<double> q, v, a;
      std::tie(q, v, a) = randQVA(type);
      testConstantAccelerationIntegration(type, step, q, v, a);
    }
  }
}

BOOST_AUTO_TEST_CASE(JacobianConsistencyTest)
{
  std::cout << "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n" << std::endl;

  for(auto type : types)
  {
    std::vector<double> q, v, a;
    std::tie(q, v, a) = randQVA(type);
    testConsistencyWithJacobian(type, q, v);
  }
}

void test(std::vector<double> q)
{
  MultiBody mb;
  MultiBodyConfig mbc;
  MultiBodyGraph mbg;
  std::tie(mb, mbc, mbg) = makeSingleJointRobot(Joint::Planar, Vector3d::UnitZ(), Eigen::Vector3d(1, 0, 0));

  mbc.q = {{}, q, {}};
  forwardKinematics(mb, mbc);
  std::cout << "(" << q[0] << " ," << q[1] << ", " << q[2] << "): " << mbc.bodyPosW[2].translation().transpose()
            << std::endl;
}

void planarTest(std::vector<double> q, std::vector<double> v, double step)
{
  const int N = 100000;
  for (int i = 0; i < N; ++i)
  {
    double c = std::cos(q[0]);
    double s = std::sin(q[0]);
    double q1Step = -q[2] * v[0] + c * v[1] - s * v[2];
    double q2Step = q[1] * v[0] + s * v[1] + c * v[2];
    q[0] += v[0] * step/N;
    q[1] += q1Step * step/N;
    q[2] += q2Step * step/N;
  }
  std::cout << "(" << q[0] << " ," << q[1] << ", " << q[2] << "): " << std::endl;
}

BOOST_AUTO_TEST_CASE(Temp)
{
  double pi = 3.141592653589793238;

  //test({0, 0, 0});
  //test({0, 1, 0});
  //test({0, 0, 1});
  //test({pi / 2, 0, 0});
  //test({pi / 2, 1, 0});

  //planarTest({pi / 2, 0, 0}, {pi / 2, 1, 0}, 1);
}