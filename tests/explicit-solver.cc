// Copyright (c) 2017, Joseph Mirabel
// Authors: Joseph Mirabel (joseph.mirabel@laas.fr)
//
// This file is part of hpp-constraints.
// hpp-constraints is free software: you can redistribute it
// and/or modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation, either version
// 3 of the License, or (at your option) any later version.
//
// hpp-constraints is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Lesser Public License for more details.  You should have
// received a copy of the GNU Lesser General Public License along with
// hpp-constraints. If not, see <http://www.gnu.org/licenses/>.

#define BOOST_TEST_MODULE EXPLICIT_SOLVER
#include <boost/test/unit_test.hpp>

#include <hpp/constraints/explicit-solver.hh>

#include <pinocchio/algorithm/joint-configuration.hpp>

#include <hpp/pinocchio/device.hh>
#include <hpp/pinocchio/joint.hh>
#include <hpp/pinocchio/liegroup.hh>
#include <hpp/pinocchio/liegroup-element.hh>
#include <hpp/pinocchio/configuration.hh>
#include <hpp/pinocchio/simple-device.hh>
#include <hpp/constraints/generic-transformation.hh>
#include <hpp/constraints/symbolic-calculus.hh>

using namespace hpp::constraints;

class LockedJoint : public DifferentiableFunction
{
  public:
    size_type idx_, length_;
    vector_t value_;

    LockedJoint(size_type idx, size_type length, vector_t value)
      : DifferentiableFunction(0, 0, LiegroupSpace::Rn (length), "LockedJoint"),
        idx_ (idx), length_ (length), value_ (value)
    {}

    ExplicitSolver::RowBlockIndices inArg () const
    {
      ExplicitSolver::RowBlockIndices ret;
      return ret;
    }

    ExplicitSolver::RowBlockIndices outArg () const
    {
      ExplicitSolver::RowBlockIndices ret;
      ret.addRow (idx_, length_);
      return ret;
    }

    ExplicitSolver::ColBlockIndices inDer () const
    {
      ExplicitSolver::ColBlockIndices ret;
      return ret;
    }

    ExplicitSolver::RowBlockIndices outDer () const
    {
      ExplicitSolver::RowBlockIndices ret;
      ret.addRow (idx_ - 1, length_);
      return ret;
    }

    void impl_compute (LiegroupElement& result, vectorIn_t ) const
    {
      result.vector () = value_;
    }

    void impl_jacobian (matrixOut_t,
                        vectorIn_t ) const
    {
      // jacobian.setIdentity();
    }
};

class TestFunction : public DifferentiableFunction
{
  public:
    size_type idxIn_, idxOut_, length_;

    TestFunction(size_type idxIn, size_type idxOut, size_type length)
      : DifferentiableFunction(length, length, LiegroupSpace::Rn (length),
                               "TestFunction"),
        idxIn_ (idxIn), idxOut_ (idxOut), length_ (length)
    {}

    ExplicitSolver::RowBlockIndices inArg () const
    {
      ExplicitSolver::RowBlockIndices ret;
      ret.addRow(idxIn_, length_);
      return ret;
    }

    ExplicitSolver::RowBlockIndices outArg () const
    {
      ExplicitSolver::RowBlockIndices ret;
      ret.addRow (idxOut_, length_);
      return ret;
    }

    ExplicitSolver::ColBlockIndices inDer () const
    {
      ExplicitSolver::ColBlockIndices ret;
      ret.addCol(idxIn_ - 1, length_); // TODO this assumes there is only the freeflyer
      return ret;
    }

    ExplicitSolver::RowBlockIndices outDer () const
    {
      ExplicitSolver::RowBlockIndices ret;
      ret.addRow (idxOut_ - 1, length_); // TODO this assumes there is only the freeflyer
      return ret;
    }

    void impl_compute (LiegroupElement& result,
                       vectorIn_t arg) const
    {
      result = LiegroupElement (arg, outputSpace ());
    }

    void impl_jacobian (matrixOut_t jacobian,
                        vectorIn_t) const
    {
      jacobian.setIdentity();
    }
};

matrix3_t exponential (const vector3_t& aa)
{
  matrix3_t R, xCross;
  xCross.setZero();
  xCross(1, 0) = + aa(2); xCross(0, 1) = - aa(2);
  xCross(2, 0) = - aa(1); xCross(0, 2) = + aa(1);
  xCross(2, 1) = + aa(0); xCross(1, 2) = - aa(0);
  R.setIdentity();
  value_type theta = aa.norm();
  if (theta < 1e-6) {
    R += xCross;
    R += 0.5 * xCross.transpose() * xCross;
  } else {
    R += sin(theta) / theta * xCross;
    R += 2 * std::pow(sin(theta/2),2) / std::pow(theta,2) * xCross * xCross;
  }
  return R;
}

class ExplicitTransformation : public DifferentiableFunction
{
  public:
    JointPtr_t joint_;
    size_type in_, inDer_;
    RelativeTransformationPtr_t rt_;

    ExplicitTransformation(JointPtr_t joint, size_type in, size_type l,
                           size_type inDer, size_type lDer)
      : DifferentiableFunction(l, lDer,
                               LiegroupSpacePtr_t (LiegroupSpace::R3 () *
                                                   LiegroupSpace::SO3 ()),
                               "ExplicitTransformation"),
        joint_ (joint), in_ (in), inDer_ (inDer)
    {
      rt_ = RelativeTransformation::create("RT", joint_->robot(),
          joint_->robot()->rootJoint(),
          joint_,
          Transform3f::Identity());
    }

    ExplicitSolver::RowBlockIndices inArg () const
    {
      ExplicitSolver::RowBlockIndices ret;
      ret.addRow(in_, inputSize());
      return ret;
    }

    ExplicitSolver::RowBlockIndices outArg () const
    {
      ExplicitSolver::RowBlockIndices ret;
      ret.addRow (0, 7);
      return ret;
    }

    ExplicitSolver::ColBlockIndices inDer () const
    {
      ExplicitSolver::ColBlockIndices ret;
      ret.addCol(inDer_, inputDerivativeSize());
      return ret;
    }

    ExplicitSolver::RowBlockIndices outDer () const
    {
      ExplicitSolver::RowBlockIndices ret;
      ret.addRow (0, 6);
      return ret;
    }

    vector_t config (vectorIn_t arg) const
    {
      vector_t q = joint_->robot()->neutralConfiguration();
      q.segment(in_, inputSize()) = arg;
      return q;
      // joint_->robot()->currentConfiguration(q);
      // joint_->robot()->computeForwardKinematics();
    }

    void impl_compute (LiegroupElement& result,
                       vectorIn_t arg) const
    {
      // forwardKinematics(arg);
      LiegroupElement transform (LiegroupSpace::Rn (6));
      vector_t q = config(arg);
      rt_->value (transform, q);
      result. vector ().head<3>() = transform. vector ().head<3>();
      result. vector ().tail<4>() =
        Eigen::Quaternion<value_type>
        (exponential(transform. vector ().tail<3>())).coeffs();

      // Transform3f tf1 = joint_->robot()->rootJoint()->currentTransformation();
      // Transform3f tf2 = joint_->currentTransformation();
      // Transform3f tf = tf2.inverse() * tf1;

      // result.head<3> = tf.translation();
      // result.tail<4> = Eigen::Quaternion<value_type>(tf.rotation());
    }

    void impl_jacobian (matrixOut_t jacobian,
                        vectorIn_t arg) const
    {
      // forwardKinematics(arg);
      matrix_t J(6, rt_->inputDerivativeSize());
      vector_t q = config(arg);
      rt_->jacobian(J, q);

      inDer().rview(J).writeTo(jacobian);
    }
};

typedef boost::shared_ptr<LockedJoint> LockedJointPtr_t;
typedef boost::shared_ptr<TestFunction> TestFunctionPtr_t;
typedef boost::shared_ptr<ExplicitTransformation> ExplicitTransformationPtr_t;

BOOST_AUTO_TEST_CASE(locked_joints)
{
  DevicePtr_t device = hpp::pinocchio::unittest::makeDevice (hpp::pinocchio::unittest::HumanoidRomeo);
  device->controlComputation((Device::Computation_t) (Device::JOINT_POSITION | Device::JACOBIAN));

  BOOST_REQUIRE (device);
  device->rootJoint()->lowerBound (0, -1);
  device->rootJoint()->lowerBound (1, -1);
  device->rootJoint()->lowerBound (2, -1);
  device->rootJoint()->upperBound (0,  1);
  device->rootJoint()->upperBound (1,  1);
  device->rootJoint()->upperBound (2,  1);

  JointPtr_t ee1 = device->getJointByName ("LAnkleRoll"),
             ee2 = device->getJointByName ("RAnkleRoll"),
             ee3 = device->getJointByName ("RAnklePitch");

  LockedJointPtr_t l1 (new LockedJoint (ee1->rankInConfiguration(), 1, vector_t::Zero(1)));
  LockedJointPtr_t l2 (new LockedJoint (ee2->rankInConfiguration(), 1, vector_t::Zero(1)));
  LockedJointPtr_t l3 (new LockedJoint (ee3->rankInConfiguration(), 1, vector_t::Zero(1)));
  TestFunctionPtr_t t1 (new TestFunction (ee1->rankInConfiguration(), ee2->rankInConfiguration(), 1));

  Configuration_t q = device->currentConfiguration (),
                  qrand = se3::randomConfiguration(device->model());

  {
    ExplicitSolver solver (device->configSize(), device->numberDof());
    BOOST_CHECK( solver.add(l1, l1->inArg(), l1->outArg(), l1->inDer(), l1->outDer()));
    BOOST_CHECK(!solver.add(l1, l1->inArg(), l1->outArg(), l1->inDer(), l1->outDer()));
    BOOST_CHECK( solver.add(l2, l2->inArg(), l2->outArg(), l2->inDer(), l2->outDer()));

    BOOST_CHECK(solver.solve(qrand));
    BOOST_CHECK_EQUAL(qrand[ee1->rankInConfiguration()], 0);
    BOOST_CHECK_EQUAL(qrand[ee2->rankInConfiguration()], 0);

    matrix_t jacobian (device->numberDof(), device->numberDof());
    solver.jacobian(jacobian, q);
    BOOST_CHECK(solver.viewJacobian (jacobian).eval().isZero());
    // std::cout << solver.viewJacobian (jacobian).eval() << '\n' << std::endl;
  }

  {
    ExplicitSolver solver (device->configSize(), device->numberDof());
    solver.difference (boost::bind(hpp::pinocchio::difference<hpp::pinocchio::LieGroupTpl>, device, _1, _2, _3));
    BOOST_CHECK( solver.add(l1, l1->inArg(), l1->outArg(), l1->inDer(), l1->outDer()));
    BOOST_CHECK( solver.add(t1, t1->inArg(), t1->outArg(), t1->inDer(), t1->outDer()));

    BOOST_CHECK(solver.solve(qrand));
    vector_t error(solver.outDers().nbIndices());
    BOOST_CHECK(solver.isSatisfied(qrand, error));
    // std::cout << error.transpose() << std::endl;
    BOOST_CHECK_EQUAL(qrand[ee1->rankInConfiguration()], 0);
    BOOST_CHECK_EQUAL(qrand[ee2->rankInConfiguration()], 0);

    matrix_t jacobian (device->numberDof(), device->numberDof());
    solver.jacobian(jacobian, q);
    BOOST_CHECK(solver.viewJacobian (jacobian).eval().isZero());
    // std::cout << solver.viewJacobian (jacobian).eval() << '\n' << std::endl;
  }

  {
    ExplicitSolver solver (device->configSize(), device->numberDof());
    BOOST_CHECK( solver.add(t1, t1->inArg(), t1->outArg(), t1->inDer(), t1->outDer()));

    matrix_t jacobian (device->numberDof(), device->numberDof());
    solver.jacobian(jacobian, q);
    BOOST_CHECK_EQUAL(jacobian(ee2->rankInVelocity(), ee1->rankInVelocity()), 1);
    BOOST_CHECK_EQUAL(solver.viewJacobian(jacobian).eval().norm(), 1);
    // std::cout << solver.viewJacobian (jacobian).eval() << '\n' << std::endl;
  }

  {
    ExplicitSolver solver (device->configSize(), device->numberDof());
    BOOST_CHECK( solver.add(t1, t1->inArg(), t1->outArg(), t1->inDer(), t1->outDer()));
    BOOST_CHECK(!solver.add(l2, l2->inArg(), l2->outArg(), l2->inDer(), l2->outDer()));
    BOOST_CHECK( solver.add(l3, l3->inArg(), l3->outArg(), l3->inDer(), l3->outDer()));

    matrix_t jacobian (device->numberDof(), device->numberDof());
    solver.jacobian(jacobian, q);
    BOOST_CHECK_EQUAL(jacobian(ee2->rankInVelocity(), ee1->rankInVelocity()), 1);
    BOOST_CHECK_EQUAL(solver.viewJacobian(jacobian).eval().norm(), 1);
    // std::cout << solver.viewJacobian (jacobian).eval() << '\n' << std::endl;
  }

  {
    // Find a joint such that the config parameters for the chain from the root
    // joint to it are the n first parameters (i.e. q.segment(0, n)).
    // We take the one which gives the longest block
    JointPtr_t parent = device->rootJoint(), current = device->getJointAtConfigRank(7);
    while (current->parentJoint()->index() == parent->index()) {
      parent = current;
      current = device->getJointAtConfigRank(current->rankInConfiguration() + current->configSize());
    }
    // std::cout << parent->name() << std::endl;

    ExplicitTransformationPtr_t et (new ExplicitTransformation (parent, 7, 6,
          parent->rankInConfiguration() + parent->configSize() - 7,
          parent->rankInVelocity()      + parent->numberDof () - 6));

    ExplicitSolver solver (device->configSize(), device->numberDof());
    BOOST_CHECK( solver.add(et, et->inArg(), et->outArg(), et->inDer(), et->outDer()));
    BOOST_CHECK( solver.add(l2, l2->inArg(), l2->outArg(), l2->inDer(), l2->outDer()));

    matrix_t jacobian (device->numberDof(), device->numberDof());
    solver.jacobian(jacobian, qrand);
    // BOOST_CHECK_EQUAL(jacobian(ee2->rankInVelocity(), ee1->rankInVelocity()), 1);
    // BOOST_CHECK_EQUAL(jacobian.norm(), 1);
    // std::cout << solver.viewJacobian (jacobian).eval() << '\n' << std::endl;
  }
}
