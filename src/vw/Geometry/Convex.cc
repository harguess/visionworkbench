// __BEGIN_LICENSE__
// 
// Copyright (C) 2006 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration
// (NASA).  All Rights Reserved.
// 
// Copyright 2006 Carnegie Mellon University. All rights reserved.
// 
// This software is distributed under the NASA Open Source Agreement
// (NOSA), version 1.3.  The NOSA has been approved by the Open Source
// Initiative.  See the file COPYING at the top of the distribution
// directory tree for the complete NOSA document.
// 
// THE SUBJECT SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF ANY
// KIND, EITHER EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT NOT
// LIMITED TO, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL CONFORM TO
// SPECIFICATIONS, ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR
// A PARTICULAR PURPOSE, OR FREEDOM FROM INFRINGEMENT, ANY WARRANTY THAT
// THE SUBJECT SOFTWARE WILL BE ERROR FREE, OR ANY WARRANTY THAT
// DOCUMENTATION, IF PROVIDED, WILL CONFORM TO THE SUBJECT SOFTWARE.
// 
// __END_LICENSE__

#include <iostream>
#include <fstream>
#include <sstream>
#include <math.h>
#include <vector>

#include <vw/config.h> // VW_HAVE_PKG_QHULL, VW_HAVE_PKG_PPL, VW_HAVE_PKG_APRON
#include <vw/Core/Thread.h> // Mutex for qhull
#include <vw/Math/Vector.h>
#include <vw/Math/Matrix.h>
#include <vw/Geometry/PointListIO.h>
#include <vw/Geometry/SpatialTree.h>
#include <vw/Geometry/Convex.h>
using namespace vw::geometry::convex_promote;

#if defined(VW_HAVE_PKG_PPL) && VW_HAVE_PKG_PPL==1
#include <gmpxx.h> // GNU Multiple Precision Arithmetic Library
#include <ppl.hh> // Parma Polyhedra Library
#elif defined(VW_HAVE_PKG_APRON) && VW_HAVE_PKG_APRON==1
#include <gmpxx.h> // GNU Multiple Precision Arithmetic Library
extern "C" {
#include <ap_global0.h> // Apron
#include <ap_global1.h>
#include <pk.h> // NewPolka
}
#endif

#if defined(VW_HAVE_PKG_QHULL) && VW_HAVE_PKG_QHULL==1
extern "C" {
#include <qhull/qhull.h>
#include <qhull/qset.h>
}
#endif

namespace vw {
namespace geometry {

  /// Square of vector 2-norm (with gmp elements).
  template <class VectorT>
  inline double norm_2_sqr_gmp( VectorBase<VectorT> const& v ) {
    double result = 0.0;
    typename VectorT::const_iterator i = v.impl().begin(), end = v.impl().end();
    for( ; i != end ; ++i ) result += (*i).get_d() * (*i).get_d();
    return result;
  }

  /// Vector 2-norm (with gmp elements).
  template <class VectorT>
  inline double norm_2_gmp( VectorBase<VectorT> const& v ) {
    return sqrt( norm_2_sqr_gmp(v) );
  }

}} // namespace vw::geometry

namespace {
  /// Mutex for qhull, which is not thread-safe.
  vw::Mutex convex_qhull_mutex;
  
  /// Point primitive for SpatialTree.
  class PointPrimitive : public vw::geometry::GeomPrimitive
  {
  public:
    PointPrimitive() {}
    
    ~PointPrimitive() {}
  
    // Implementation of GeomPrimitive interface.
    virtual double distance(const vw::Vector<double> &point_) const
    {
      using namespace vw::math;
      return norm_2(point - point_);
    }
    virtual const vw::BoxN &bounding_box() const {return bbox;}
  
    unsigned index;
    vw::Vector<double> point;
    vw::BoxN bbox;
  };
  
  /// Indexed polyhedron edge.
  struct IndexedEdge {
    unsigned index[2];
    bool used;
  };

  /// Find the least common denominator.
  mpz_class least_common_denominator( vw::math::Vector<mpq_class> const& v ) {
    unsigned dim = v.size();
    VW_ASSERT(dim > 0, vw::LogicErr() << "Cannot find least common denominator of 0 points!");
    mpz_class den = v(0).get_den();
    for (unsigned i = 1; i < dim; i++) {
      mpq_class q(v(i).get_den(), den);
      q.canonicalize();
      den = q.get_den() * v(i).get_den();
    }
    return den;
  }

  /// Convert all rationals in Vector so that they have a common denominator.
  //NOTE: The resulting rationals may not be canonical, and arithmetic expressions
  //  involving them may not work properly. This is desired behavior, as we want
  //  all rationals in the Vector to have the same denominator.
  vw::math::Vector<mpq_class> const& common_denominator( vw::math::Vector<mpq_class> const& point, vw::math::Vector<mpq_class> &p ) {
    unsigned dim = point.size();
    p.set_size(dim);
    if (dim > 0) {
      mpz_class lcd = least_common_denominator(point);
      for (unsigned i = 0; i < dim; i++) {
        p(i).get_num() = point(i).get_num() * (lcd / point(i).get_den());
        p(i).get_den() = lcd;
      }
    }
    return p;
  }

  /// Convert all rationals in Vector so that they have no denominator
  /// (so that linear expression with rationals as coefficients is equivalent).
  vw::math::Vector<mpz_class> const& no_denominator( vw::math::Vector<mpq_class> const& point, vw::math::Vector<mpz_class> &p ) {
    unsigned dim = point.size();
    p.set_size(dim);
    if (dim > 0) {
      mpz_class lcd = least_common_denominator(point);
      for (unsigned i = 0; i < dim; i++) {
        p(i) = point(i).get_num() * (lcd / point(i).get_den());
      }
    }
    return p;
  }

  /// Converts a Promoted Vector to an mpq_class Vector.
  vw::math::Vector<mpq_class> const& convert_vector( vw::math::Vector<Promoted> const& point, vw::math::Vector<mpq_class> &p ) {
    unsigned dim = point.size();
    p.set_size(dim);
    if (!point(0).is_integral) {
      for (unsigned i = 0; i < dim; i++)
        p(i) = mpq_class(point(i).val.f);
    }
    else if (point(0).is_signed) {
      for (unsigned i = 0; i < dim; i++)
        p(i) = mpq_class(point(i).val.s);
    }
    else {
      for (unsigned i = 0; i < dim; i++)
        p(i) = mpq_class(point(i).val.u);
    }
    return p;
  }

  /// Converts a double Vector to an mpq_class Vector.
  vw::math::Vector<mpq_class> const& convert_vector( vw::math::Vector<double> const& point, vw::math::Vector<mpq_class> &p ) {
    unsigned dim = point.size();
    p.set_size(dim);
    for (unsigned i = 0; i < dim; i++)
      p(i) = mpq_class(point(i));
    return p;
  }

  /// Converts an mpq_class Vector to a double Vector.
  vw::math::Vector<double> const& unconvert_vector( vw::math::Vector<mpq_class> const& point, vw::math::Vector<double> &p ) {
    unsigned dim = point.size();
    p.set_size(dim);
    for (unsigned i = 0; i < dim; i++)
      p(i) = point(i).get_d();
    return p;
  }

  /// Converts a Promoted to an mpq_class.
  mpq_class const& convert_scalar( Promoted const& scalar, mpq_class &s ) {
    if (!scalar.is_integral) {
      s = mpq_class(scalar.val.f);
    }
    else if (scalar.is_signed) {
      s = mpq_class(scalar.val.s);
    }
    else {
      s = mpq_class(scalar.val.u);
    }
    return s;
  }

  /// Converts a double to an mpq_class.
  mpq_class const& convert_scalar( double const& scalar, mpq_class &s ) {
    s = mpq_class(scalar);
    return s;
  }

  /// Converts an mpq_class to a double.
  double const& unconvert_scalar( mpq_class const& scalar, double &s ) {
    s = scalar.get_d();
    return s;
  }

#if defined(VW_HAVE_PKG_PPL) && VW_HAVE_PKG_PPL==1

  /// All functionality that is tied to PPL.
  class ConvexImpl
  {
  public:
    /// Constructor.
    ConvexImpl( unsigned dim, bool universe = false ) : poly( dim, universe ? Parma_Polyhedra_Library::UNIVERSE : Parma_Polyhedra_Library::EMPTY ) {}

    /// Copy constructor.
    ConvexImpl( const ConvexImpl &other ) : poly(other.poly) {}

    /// Destructor.
    ~ConvexImpl() {}

    /// Assignment operator.
    ConvexImpl &operator=( const ConvexImpl &other ) {poly = other.poly;}

    /// Return dimension of ambient space.
    inline unsigned space_dimension() const {return poly.space_dimension();}

    /// Return whether polyhedron is the universe polyhedron.
    inline bool is_universe() const {return poly.is_universe();}

    /// Return whether polyhedron is empty.
    inline bool is_empty() const {return poly.is_empty();}

    /// Return whether the polyhedron contains the given point.
    inline bool contains( const vw::Vector<mpq_class> &point ) const {
      return (poly.relation_with(point_generator(point)) == Parma_Polyhedra_Library::Poly_Gen_Relation::subsumes());
    }
    
    /// Return whether the polyhedron contains the other polyhedron.
    inline bool contains( const ConvexImpl &other ) const {return poly.contains(other.poly);}

    /// Return whether the polyhedron is disjoint from the other polyhedron.
    inline bool is_disjoint_from( const ConvexImpl &other ) const {return poly.is_disjoint_from(other.poly);}

    /// Return whether the polyhedron is equal to the other polyhedron.
    inline bool equal( const ConvexImpl &other ) const {
      using Parma_Polyhedra_Library::operator==;
      return (poly == other.poly);
    }

    /// Return the number of constraints.
    inline unsigned num_constraints() const {
      return num_constraints(poly.minimized_constraints(), true);
    }

    /// Add a constraint.
    inline void add_constraint( const vw::Vector<mpq_class> &coef ) {
      poly.add_constraint(constraint_generator(coef));
    }

    /// Add multiple constraints.
    inline void add_constraints( const std::vector<vw::Vector<mpq_class> > &constraints ) {
      std::vector<vw::Vector<mpq_class> >::const_iterator i;
      for (i = constraints.begin(); i != constraints.end(); i++)
        add_constraint(*i);
    }

    /// Get all constraints.
    std::vector<vw::Vector<mpz_class> > &get_constraints( std::vector<vw::Vector<mpz_class> > &constraints ) const {
      vw::Vector<mpz_class> coef;
      const Parma_Polyhedra_Library::Constraint_System &cs = poly.minimized_constraints();
      Parma_Polyhedra_Library::Constraint_System::const_iterator i;
      constraints.clear();
      for (i = cs.begin(); i != cs.end(); i++) {
        VW_ASSERT(!(*i).is_strict_inequality(), vw::LogicErr() << "Retrieved strict inequality from closed convex polyhedron!");
        coefficients(*i, coef);
        constraints.push_back(coef);
        if ((*i).is_equality())
          constraints.push_back(-coef);
      }
      return constraints;
    }

    /// Return the number of generators.
    inline unsigned num_generators() const {
      return num_generators(poly.minimized_generators());
    }

    /// Add a (point) generator.
    inline void add_generator( const vw::Vector<mpq_class> &point ) {
      poly.add_generator(point_generator(point));
    }

    /// Add multiple (point) generators.
    inline void add_generators( const std::vector<vw::Vector<mpq_class> > &generators ) {
      std::vector<vw::Vector<mpq_class> >::const_iterator i;
      for (i = generators.begin(); i != generators.end(); i++)
        add_generator(*i);
    }

    /// Get all (point) generators.
    std::vector<vw::Vector<mpq_class> > &get_generators( std::vector<vw::Vector<mpq_class> > &generators ) const {
      vw::Vector<mpq_class> p;
      const Parma_Polyhedra_Library::Generator_System &gs = poly.minimized_generators();
      Parma_Polyhedra_Library::Generator_System::const_iterator i;
      generators.clear();
      for (i = gs.begin(); i != gs.end(); i++) {
        VW_ASSERT((*i).is_point(), vw::LogicErr() << "Retrieved non-point generator from closed convex polyhedron!");
        convert_point(*i, p);
        generators.push_back(p);
      }
      return generators;
    }

    /// Assign to this polyhedron the convex hull of this polyhedron and the other polyhedron.
    inline void poly_hull_assign( const ConvexImpl &other ) {poly.poly_hull_assign(other.poly);}

    /// Assign to this polyhedron the intersection of this polyhedron and the other polyhedron.
    inline void intersection_assign( const ConvexImpl &other ) {poly.intersection_assign(other.poly);}

    /// Print the polyhedron.
    void print( std::ostream& os ) const {
      using ::Parma_Polyhedra_Library::IO_Operators::operator<<;
      os << poly;
    }

  private:
    /// Creates a PPL point.
    static Parma_Polyhedra_Library::Generator point_generator( vw::math::Vector<mpq_class> const& point ) {
      using namespace Parma_Polyhedra_Library;
      Linear_Expression e;
      unsigned dim = point.size();
      vw::math::Vector<mpq_class> pointc( dim );
      common_denominator(point, pointc);
      for (unsigned i = point.size() - 1; 1; i--) {
        e += pointc(i).get_num() * Variable(i);
        if (i == 0)
          break;
      }
      Generator g = ::Parma_Polyhedra_Library::point(e, pointc(0).get_den());
      return g;
    }
  
    /// Creates a PPL Constraint from an mpz_class Vector.
    static Parma_Polyhedra_Library::Constraint constraint_generator( vw::math::Vector<mpz_class> const& coef ) {
      using namespace Parma_Polyhedra_Library;
      unsigned dim = coef.size() - 1;
      Linear_Expression e;
      unsigned i;
      for (i = dim - 1; 1; i--) {
        e += (Variable(i) * coef[i]);
        if (i == 0)
          break;
      }
      e += coef[dim];
      Constraint c = (e >= 0);
      return c;
    }
  
    /// Creates a PPL Constraint from an mpq_class Vector.
    static Parma_Polyhedra_Library::Constraint constraint_generator( vw::math::Vector<mpq_class> const& coef ) {
      vw::math::Vector<mpz_class> coefz( coef.size() );
      no_denominator(coef, coefz);
      return constraint_generator(coefz);
    }
  
    /// Finds the coefficients of a PPL Constraint.
    static void coefficients( Parma_Polyhedra_Library::Constraint const& c, vw::math::Vector<mpz_class> &coef ) {
      VW_ASSERT(!c.is_strict_inequality(), vw::LogicErr() << "Retrieved strict inequality from closed convex polyhedron!");
      unsigned dim = c.space_dimension();
      unsigned i;
      coef.set_size(dim + 1);
      for (i = dim - 1; 1; i--) {
        coef[i] = c.coefficient(Parma_Polyhedra_Library::Variable(i));
        if (i == 0)
          break;
      }
      coef[dim] = c.inhomogeneous_term();
    }
  
    /// Finds the coefficients of a PPL Constraint.
    static void coefficients( Parma_Polyhedra_Library::Constraint const& c, vw::math::Vector<mpq_class> &coef ) {
      VW_ASSERT(!c.is_strict_inequality(), vw::LogicErr() << "Retrieved strict inequality from closed convex polyhedron!");
      unsigned dim = c.space_dimension();
      unsigned i;
      coef.set_size(dim + 1);
      for (i = dim - 1; 1; i--) {
        coef[i] = mpq_class(c.coefficient(Parma_Polyhedra_Library::Variable(i)), 1);
        if (i == 0)
          break;
      }
      coef[dim] = mpq_class(c.inhomogeneous_term(), 1);
    }
  
    /// Returns the PPL Generator (point) as a Vector.
    static void convert_point( Parma_Polyhedra_Library::Generator const& g, vw::Vector<mpq_class> &p ) {
      VW_ASSERT(g.is_point(), vw::LogicErr() << "Retrieved non-point generator from closed convex polyhedron!");
      unsigned dim = g.space_dimension();
      unsigned i;
      p.set_size(dim);
      for (i = dim - 1; 1; i--) {
        p(i) = mpq_class(g.coefficient(Parma_Polyhedra_Library::Variable(i)), g.divisor());
        if (i == 0)
          break;
      }
    }
  
    /// Finds the number of constraints in a constraint system.
    static inline unsigned num_constraints( Parma_Polyhedra_Library::Constraint_System const& cs, bool count_equalities_twice = false ) {
      unsigned n = 0;
      Parma_Polyhedra_Library::Constraint_System::const_iterator i;
      for (i = cs.begin(); i != cs.end(); i++) {
        VW_ASSERT(!(*i).is_strict_inequality(), vw::LogicErr() << "Retrieved strict inequality from closed convex polyhedron!");
        n++;
        if ((*i).is_equality() && count_equalities_twice)
          n++;
      }
      return n;
    }
  
    /// Finds the number of generators in a generator system.
    static inline unsigned num_generators( Parma_Polyhedra_Library::Generator_System const& gs ) {
      unsigned n = 0;
      Parma_Polyhedra_Library::Generator_System::const_iterator i;
      for (i = gs.begin(); i != gs.end(); i++) {
        VW_ASSERT((*i).is_point(), vw::LogicErr() << "Retrieved non-point generator from closed convex polyhedron!");
        n++;
      }
      return n;
    }

    Parma_Polyhedra_Library::C_Polyhedron poly;
  };

#elif defined(VW_HAVE_PKG_APRON) && VW_HAVE_PKG_APRON==1

  /// All functionality that is tied to Apron/NewPolka.
  class ConvexImpl {
  public:
    /// Constructor.
    ConvexImpl( unsigned dim_, bool universe = false ) : dim( dim_ ) {
      generate_variable_names();
      man = pk_manager_alloc(false); // no strict inequalities
      env = ap_environment_alloc(0, 0, variable_names, dim);
      poly = universe ? ap_abstract1_top(man, env) : ap_abstract1_bottom(man, env);
    }
  
    /// Copy constructor.
    ConvexImpl( const ConvexImpl &other ) : dim( other.dim ) {
      generate_variable_names();
      man = pk_manager_alloc(false); // no strict constraints
      env = ap_environment_alloc(NULL, 0, variable_names, other.dim);
      poly = ap_abstract1_copy(other.man, &other.poly);
    }

    /// Destructor.
    ~ConvexImpl() {
      ap_abstract1_clear(man, &poly);
      ap_environment_free(env);
      ap_manager_free(man);
      cleanup_variable_names();
    }

    /// Assignment operator.
    ConvexImpl &operator=( const ConvexImpl &other ) {
      if (&other != (const ConvexImpl*)this) {
        ap_abstract1_clear(man, &poly);
        ap_environment_free(env);
        ap_manager_free(man);
        cleanup_variable_names();
        dim = other.dim;
        generate_variable_names();
        man = pk_manager_alloc(false); // no strict constraints
        env = ap_environment_alloc(NULL, 0, variable_names, other.dim);
        poly = ap_abstract1_copy(other.man, &other.poly);
      }
      return *this;
    }

    /// Return dimension of ambient space.
    inline unsigned space_dimension() const {return dim;}

    /// Return whether polyhedron is the universe polyhedron.
    inline bool is_universe() const {return ap_abstract1_is_top(man, &poly);}

    /// Return whether polyhedron is empty.
    inline bool is_empty() const {return ap_abstract1_is_bottom(man, &poly);}

    /// Return whether the polyhedron contains the given point.
    inline bool contains( const vw::Vector<mpq_class> &point ) const {
      ConvexImpl point_poly(point.size(), true);
      return contains(point_polyhedron_generator(point, point_poly));
    }
    
    /// Return whether the polyhedron contains the other polyhedron.
    inline bool contains( const ConvexImpl &other ) const {
      VW_ASSERT(ap_environment_is_eq(env, other.env), vw::ArgumentErr() << "Cannot compare convex shapes with different environments!");
      return ap_abstract1_is_leq(man, &other.poly, &poly);
    }

    /// Return whether the polyhedron is disjoint from the other polyhedron.
    bool is_disjoint_from( const ConvexImpl &other ) const {
      VW_ASSERT(ap_environment_is_eq(env, other.env), vw::ArgumentErr() << "Cannot compare convex shapes with different environments!");
      bool disjoint;
      ap_abstract1_t intersection;
      intersection = ap_abstract1_meet(man, false, &poly, &other.poly);
      disjoint = ap_abstract1_is_bottom(man, &intersection);
      ap_abstract1_clear(man, &intersection);
      return disjoint;
    }

    /// Return whether the polyhedron is equal to the other polyhedron.
    inline bool equal( const ConvexImpl &other ) const {
      VW_ASSERT(ap_environment_is_eq(env, other.env), vw::ArgumentErr() << "Cannot compare convex shapes with different environments!");
      return ap_abstract1_is_eq(man, &other.poly, &poly);
    }

    /// Return the number of constraints.
    unsigned num_constraints() const {
      unsigned n;
      ap_lincons1_array_t cs = ap_abstract1_to_lincons_array(man, &poly);
      n = num_constraints(&cs);
      ap_lincons1_array_clear(&cs);
      return n;
    }
  
    /// Add a constraint.
    void add_constraint( const vw::Vector<mpq_class> &coef ) {
      VW_ASSERT(coef.size() == dim + 1, vw::ArgumentErr() << "Cannot add constraint of different dimension!");
      ap_lincons1_array_t cs = ap_lincons1_array_make(env, 1);
      ap_lincons1_t c = constraint_generator(coef);
      ap_lincons1_array_set(&cs, 0, &c);
      poly = ap_abstract1_meet_lincons_array(man, true, &poly, &cs);
      ap_lincons1_array_clear(&cs);
    }
  
    /// Add multiple constraints.
    void add_constraints( const std::vector<vw::Vector<mpq_class> > &constraints ) {
      ap_lincons1_array_t cs = ap_lincons1_array_make(env, constraints.size());
      ap_lincons1_t c;
      for (unsigned i = 0; i < constraints.size(); i++) {
        VW_ASSERT(constraints[i].size() == dim + 1, vw::ArgumentErr() << "Cannot add constraint of different dimension!");
        c = constraint_generator(constraints[i]);
        ap_lincons1_array_set(&cs, i, &c); //NOTE: even though c is passed as a pointer, it is OK to reuse it
      }
      poly = ap_abstract1_meet_lincons_array(man, true, &poly, &cs);
      ap_lincons1_array_clear(&cs);
    }
  
    /// Get all constraints.
    std::vector<vw::Vector<mpz_class> > &get_constraints( std::vector<vw::Vector<mpz_class> > &constraints ) const {
      ap_lincons1_array_t cs = ap_abstract1_to_lincons_array(man, &poly);
      ap_lincons1_t c;
      ap_constyp_t *t;
      vw::Vector<mpz_class> coef;
      constraints.clear();
      for (unsigned i = 0; i < ap_lincons1_array_size(&cs); i++) {
        c = ap_lincons1_array_get(&cs, i);
        t = ap_lincons1_constypref(&c);
        VW_ASSERT(*t == AP_CONS_EQ || *t == AP_CONS_SUPEQ, vw::LogicErr() << "Retrieved strict inequality from closed convex polyhedron!");
        coefficients(&c, coef);
        constraints.push_back(coef);
        if (*t == AP_CONS_EQ)
          constraints.push_back(-coef);
      }
      ap_lincons1_array_clear(&cs);
    }

//#if 0
    /// Return the number of generators.
    unsigned num_generators() const {
      unsigned n;
      ap_generator1_array_t gs = ap_abstract1_to_generator_array(man, &poly);
      n = num_generators(&gs);
      ap_generator1_array_clear(&gs);
      return n;
    }

    /// Add a (point) generator.
    void add_generator( const vw::Vector<mpq_class> &point ) {
      VW_ASSERT(point.size() == dim, vw::ArgumentErr() << "Cannot add point of different dimension!");
      ConvexImpl point_poly(point.size(), true);
      poly_hull_assign(point_polyhedron_generator(point, point_poly));
#if 0
      ap_generator1_array_t old_gs = ap_abstract1_to_generator_array(man, &poly);
      unsigned old_n = num_generators(&old_gs);
      unsigned new_n = old_n + 1;
      ap_generator1_array_t new_gs = ap_generator1_array_make(env, new_n);
      ap_generator1_t old_g, new_g;
      unsigned i;
      for (i = 0; i < old_n; i++) {
        old_g = ap_lincons1_array_get(&old_gs, i);
        new_g = ap_generator1_copy(&old_g);
        ap_generator1_array_set(&new_gs, i, &new_g); //NOTE: even though new_g is passed as a pointer, it is OK to reuse it
      }
      ap_generator1_array_clear(&old_gs);


      ap_lincons1_array_t gs = ap_lincons1_array_make(env, 1);
      ap_lincons1_t c = constraint_generator(coef);
      ap_lincons1_array_set(&cs, 0, &c);
      poly = ap_abstract1_meet_lincons_array(man, true, &poly, &cs);
      
      ap_generator1_array_clear(&new_gs); //FIXME: do this?
#endif
    }

    /// Add multiple (point) generators.
    inline void add_generators( const std::vector<vw::Vector<mpq_class> > &generators ) {
      std::vector<vw::Vector<mpq_class> >::const_iterator i;
      for (i = generators.begin(); i != generators.end(); i++)
        add_generator(*i);
    }

    /// Get all (point) generators.
    std::vector<vw::Vector<mpq_class> > &get_generators( std::vector<vw::Vector<mpq_class> > &generators ) const {
      vw::Vector<mpq_class> p;
      ap_generator1_array_t gs = ap_abstract1_to_generator_array(man, &poly);
      ap_generator1_t g;
      ap_gentyp_t *t;
      generators.clear();
      for (unsigned i = 0; i < ap_generator1_array_size(&gs); i++) {
        g = ap_generator1_array_get(&gs, i);
        t = ap_generator1_gentypref(&g);
        VW_ASSERT(*t == AP_GEN_VERTEX, vw::LogicErr() << "Retrieved non-point generator from closed convex polyhedron!");
        convert_point(&g, p);
        generators.push_back(p);
      }
      return generators;
    }
//#endif

    /// Assign to this polyhedron the convex hull of this polyhedron and the other polyhedron.
    inline void poly_hull_assign( const ConvexImpl &other ) {
      VW_ASSERT(ap_environment_is_eq(env, other.env), vw::ArgumentErr() << "Cannot combine convex shapes with different environments!");
      poly = ap_abstract1_join(man, true, &poly, &other.poly);
    }

    /// Assign to this polyhedron the intersection of this polyhedron and the other polyhedron.
    inline void intersection_assign( const ConvexImpl &other ) {
      VW_ASSERT(ap_environment_is_eq(env, other.env), vw::ArgumentErr() << "Cannot combine convex shapes with different environments!");
      poly = ap_abstract1_meet(man, true, &poly, &other.poly);
    }

    /// Print the polyhedron.
    void print( std::ostream& os ) const {
      if (is_empty()) {
        os << "false";
        return;
      }
      if (is_universe()) {
        os << "true";
        return;
      }
      unsigned i, j;
      std::vector<vw::Vector<mpz_class> > constraints;
      get_constraints(constraints);
      for (i = 0; i < constraints.size(); i++) {
        for (j = 0; j < constraints[i].size() - 1; j++)
          os << constraints[i][j].get_str() << "*" << std::string((char*)(variable_names[j])) << " + ";
        os << constraints[i][j].get_str() << " >= 0";
        if (i < constraints.size() - 1)
          os << ", ";
      }
    }

//#if 0
    //FIXME: get rid of this
    void print() const {
      ap_abstract1_fprint(stdout, man, &poly);
      printf("\n"); //FIXME: get rid of this
    }
//#endif
  
  private:
    /// Generate a variable name for each dimension.
    void generate_variable_names() {
      char i;
      unsigned j, k;
      VW_ASSERT(dim <= 26, vw::LogicErr() << "Unable to create more than 26 variable names!");
      variable_names_char = (char*)malloc(2 * dim * sizeof(char));
      variable_names = (ap_var_t*)malloc(dim * sizeof(ap_var_t));
      for (j = 0, k = 1; j < dim; j++, k += 2)
        variable_names_char[k] = '\0';
      for (i = 0, j = 0, k = 0; i < 3 && j < dim; i++, j++, k += 2) {
        variable_names_char[k] = 'X' + i;
        variable_names[j] = (ap_var_t)&variable_names_char[k];
      }
      for (i = 0; i < 3 && j < dim; i++, j++, k += 2) {
        variable_names_char[k] = 'U' + i;
        variable_names[j] = (ap_var_t)&variable_names_char[k];
      }
      for (i = 0; i < 20 && j < dim; i++, j++, k += 2) {
        variable_names_char[k] = 'A' + i;
        variable_names[j] = (ap_var_t)&variable_names_char[k];
      }
    }
  
    /// Cleanup variable names for each dimension.
    void cleanup_variable_names() {
      free(variable_names);
      variable_names = 0;
      free(variable_names_char);
      variable_names_char = 0;
    }
  
    /// Creates an Apron scalar.
    static ap_scalar_t *scalar_generator( const mpq_class &scalar ) {
      ap_scalar_t *s = ap_scalar_alloc();
      mpq_class q = scalar;
      ap_scalar_set_mpq(s, q.get_mpq_t());
      return s;
    }
    
#if 0
    /// Creates an Apron point.
    ap_generator1_t point_generator( const vw::Vector<mpq_class> &point ) const {
      ap_linexpr1_t *e = (ap_linexpr1_t*)malloc(sizeof(ap_linexpr1_t));
      *e = ap_linexpr1_make(env, AP_LINEXPR_DENSE, point.size());
      ap_linexpr0_t *e0 = ap_linexpr1_linexpr0ref(e);
      unsigned i;
      for (i = 0; i < point.size(); i++)
        ap_linexpr0_set_coeff_scalar(e0, i, scalar_generator(point[i]));
      ap_linexpr0_set_cst_scalar(e0, scalar_generator(0)); //FIXME: ???
      ap_generator1_t g = ap_generator1_make(AP_GEN_VERTEX, e);
      //NOTE: e is not duplicated; it will be freed when g is cleaned up
      return g;
    }
#endif

    /// Make a polyhedron that contains a single point.
    static ConvexImpl &point_polyhedron_generator( const vw::Vector<mpq_class> &point, ConvexImpl &point_poly ) {
      std::vector<vw::Vector<mpq_class> > constraints;
      vw::Vector<mpq_class> coef1, coef2;
      coef1.set_size(point.size() + 1);
      coef2.set_size(point.size() + 1);
      for (unsigned i = 0; i < point.size(); i++) {
        coef1[i] = 0;
        coef2[i] = 0;
      }
      for (unsigned i = 0; i < point.size(); i++) {
        coef1[i] = -1;
        coef2[i] = 1;
        coef1[point.size()] = point[i];
        coef2[point.size()] = -point[i];
        constraints.push_back(coef1);
        constraints.push_back(coef2);
        coef1[i] = 0;
        coef2[i] = 0;
      }
      point_poly.add_constraints(constraints);
#if 0
      point_poly.print();
      std::vector<vw::Vector<mpq_class> > gs;
      point_poly.get_generators(gs);
      std::cout << "Has " << point_poly.num_generators() << " generators:" << std::endl;
      for (unsigned i = 0; i < gs.size(); i++) {
        for (unsigned j = 0; j < gs[i].size(); j++)
          std::cout << gs[i][j].get_str() << " ";
        std::cout << std::endl;
      }
#endif
      return point_poly;
    }
    
    /// Creates an Apron constraint.
    ap_lincons1_t constraint_generator( const vw::Vector<mpq_class> &coef ) const {
      ap_linexpr1_t *e = (ap_linexpr1_t*)malloc(sizeof(ap_linexpr1_t));
      *e = ap_linexpr1_make(env, AP_LINEXPR_DENSE, coef.size() - 1);
      ap_linexpr0_t *e0 = ap_linexpr1_linexpr0ref(e);
      unsigned i;
      for (i = 0; i < (coef.size() - 1); i++)
        ap_linexpr0_set_coeff_scalar(e0, i, scalar_generator(coef[i]));
      ap_linexpr0_set_cst_scalar(e0, scalar_generator(coef[i]));
      ap_lincons1_t c = ap_lincons1_make(AP_CONS_SUPEQ, e, 0);
      //NOTE: e is not duplicated; it will be freed when c is cleaned up
      return c;
    }
    
    /// Finds the value of an Apron scalar.
    static mpq_class &convert_scalar( ap_scalar_t *s, mpq_class &scalar ) {
      VW_ASSERT(s->discr == AP_SCALAR_MPQ, vw::ArgumentErr() << "Trying to convert scalar of wrong type!");
      scalar = mpq_class(s->val.mpq);
      return scalar;
    }
    
    /// Finds the coefficients of an Apron constraint.
    void coefficients( ap_lincons1_t *c, vw::Vector<mpz_class> &coef ) const {
      ap_lincons0_t *c0 = ap_lincons1_lincons0ref(c);
      ap_linexpr1_t e = ap_lincons1_linexpr1ref(c);
      ap_linexpr0_t *e0 = ap_linexpr1_linexpr0ref(&e);
      ap_coeff_t *a = ap_coeff_alloc(AP_COEFF_SCALAR);
      mpq_class q;
      unsigned i;
      VW_ASSERT(ap_linexpr0_is_linear(e0), vw::ArgumentErr() << "Linear expression is not linear!");
      coef.set_size(dim + 1);
      for (i = 0; i < dim; i++) {
        ap_linexpr0_get_coeff(a, e0, i);
        VW_ASSERT(a->discr == AP_COEFF_SCALAR, vw::ArgumentErr() << "Linear expression is not linear!");
        convert_scalar(a->val.scalar, q);
        VW_ASSERT(q.get_den() == 1, vw::ArgumentErr() << "Linear expression has non-integer coefficients!");
        coef[i] = q.get_num();
      }
      ap_linexpr0_get_cst(a, e0);
      VW_ASSERT(a->discr == AP_COEFF_SCALAR, vw::ArgumentErr() << "Linear expression is not linear!");
      convert_scalar(a->val.scalar, q);
      VW_ASSERT(q.get_den() == 1, vw::ArgumentErr() << "Linear expression has non-integer coefficients!");
      coef[i] = q.get_num();
      ap_coeff_free(a);
    }
    
    /// Returns the Apron generator (point) as a Vector.
    void convert_point( ap_generator1_t *g, vw::Vector<mpq_class> &p ) const {
      ap_generator0_t *g0 = ap_generator1_generator0ref(g);
      ap_linexpr1_t e = ap_generator1_linexpr1ref(g);
      ap_linexpr0_t *e0 = ap_linexpr1_linexpr0ref(&e);
      ap_gentyp_t *t = ap_generator1_gentypref(g);
      ap_coeff_t *a = ap_coeff_alloc(AP_COEFF_SCALAR);
      unsigned i;
      VW_ASSERT(*t == AP_GEN_VERTEX, vw::LogicErr() << "Retrieved non-point generator from closed convex polyhedron!");
      VW_ASSERT(ap_linexpr0_is_linear(e0), vw::ArgumentErr() << "Linear expression is not linear!");
      p.set_size(dim);
      for (i = 0; i < dim; i++) {
        ap_linexpr0_get_coeff(a, e0, i);
        VW_ASSERT(a->discr == AP_COEFF_SCALAR, vw::ArgumentErr() << "Linear expression is not linear!");
        convert_scalar(a->val.scalar, p[i]);
      }
      ap_coeff_free(a);
    }

    /// Finds the number of constraints in a constraint system.
    static unsigned num_constraints( ap_lincons1_array_t *cs ) {
      ap_lincons1_t c;
      ap_constyp_t *t;
      unsigned n = 0;
      for (unsigned i = 0; i < ap_lincons1_array_size(cs); i++) {
        c = ap_lincons1_array_get(cs, i);
        t = ap_lincons1_constypref(&c);
        VW_ASSERT(*t == AP_CONS_EQ || *t == AP_CONS_SUPEQ, vw::LogicErr() << "Retrieved strict inequality from closed convex polyhedron!");
        n++;
        if (*t == AP_CONS_EQ)
          n++;
      }
      return n;
    }

    /// Finds the number of generators in a generator system.
    static unsigned num_generators( ap_generator1_array_t *gs ) {
      ap_generator1_t g;
      ap_gentyp_t *t;
      unsigned n = 0;
      for (unsigned i = 0; i < ap_generator1_array_size(gs); i++) {
        g = ap_generator1_array_get(gs, i);
        t = ap_generator1_gentypref(&g);
        VW_ASSERT(*t == AP_GEN_VERTEX, vw::LogicErr() << "Retrieved non-point generator from closed convex polyhedron!");
        n++;
      }
      return n;
    }
  
    unsigned dim;
    char *variable_names_char;
    ap_var_t *variable_names;
    ap_manager_t *man;
    ap_environment_t *env;
    mutable ap_abstract1_t poly;
  };

#endif // defined(VW_HAVE_PKG_PPL) && VW_HAVE_PKG_PPL==1

#if defined(VW_HAVE_PKG_QHULL) && VW_HAVE_PKG_QHULL==1

  /// Runs qhull.
  void qhull_run( unsigned dim, unsigned num_points, double *p ) {
    int retval;
    FILE *fake_stdout;
    FILE *fake_stderr;
    unsigned i;
  
    fake_stdout = tmpfile();
    if (!fake_stdout)
      fake_stdout = stdout;
    fake_stderr = tmpfile();
    if (!fake_stderr)
      fake_stderr = stderr;
    retval = qh_new_qhull((int)dim, (int)num_points,
                          p, False, "qhull s Tcv", fake_stdout, fake_stderr);
    if (retval != 0 && fake_stderr != stderr) {
      std::string qhull_error;
      int c;
      rewind(fake_stderr);
      while ((c = fgetc(fake_stderr)) != EOF)
        qhull_error.append(1, (char)c);
      VW_ASSERT(retval == 0, vw::LogicErr() << "qhull returned with error: " << qhull_error);
    }
    VW_ASSERT(retval == 0, vw::LogicErr() << "qhull returned with error.");
    if (fake_stdout != stdout)
      fclose(fake_stdout);
    if (fake_stderr != stderr)
      fclose(fake_stderr);
  }

  /// Frees qhull memory.
  void qhull_free() {
    int curlong, totlong;
    qh_freeqhull(!qh_ALL);
    qh_memfreeshort(&curlong, &totlong);
    VW_ASSERT(curlong == 0 && totlong == 0, vw::LogicErr() << "qhull did not free all of its memory");
  }
  
#endif // defined(VW_HAVE_PKG_QHULL) && VW_HAVE_PKG_QHULL==1

  /// Find the center of a polyhedron.
  void poly_center( const std::vector<vw::Vector<mpq_class> > &gs, vw::Vector<mpq_class> &c ) {
    VW_ASSERT(!gs.empty(), vw::LogicErr() << "Generator list is empty!");
    unsigned dim = gs[0].size();
    unsigned n = 0;
    c.set_size(dim);
    fill(c, 0);
    for (std::vector<vw::Vector<mpq_class> >::const_iterator i = gs.begin(); i != gs.end(); i++) {
      c += *i;
      n++;
    }
    c /= n;
  }

  /// Find an orthonormal basis using a given Vector.
  void gram_schmidt( vw::Vector<double> const& v, vw::Matrix<double> &b ) {
    using namespace vw;
    using namespace ::vw::math;
    unsigned dim = v.size();
    unsigned largest;
    unsigned row, col, col2;
    double d;
    b.set_size(dim, dim);
    select_col(b, 0) = v;
    select_col(b, 0) /= norm_2(select_col(b, 0));
    largest = index_norm_inf(select_col(b, 0));
    for (row = 0, col = 1; col < dim; row++, col++) {
      if (row == largest)
        row++;
      select_col(b, col) = select_col(b, 0);
      b(row, col) += 1.0;
      for (col2 = 0; col2 < col; col2++) {
        d = dot_prod(select_col(b, col), select_col(b, col2));
        select_col(b, col) -= elem_prod(d, select_col(b, col2));
      }
      select_col(b, col) /= norm_2(select_col(b, col));
      //for (col2 = 0; col2 < col; col2++) {
      //  VW_ASSERT(dot_prod(select_col(b, col), select_col(b, col2)) == 0, LogicErr() << "gram_schmidt failed!");
      //}
    }
  }

  /// Undo projection to plane.
  void plane_unproject( unsigned dim, vw::Vector<double> const& v, vw::Matrix<double> const& basis, vw::Vector<double> const& plane_origin, vw::Vector<double> &p ) {
    unsigned i;
    if (dim == 2) {
      p.set_size(2);
      for (i = 0; i < dim; i++)
        p[i] = v[i];
    }
    else {
      p = plane_origin;
      for (i = 0; i < dim - 1; i++)
        p += elem_prod(v[i], select_col(basis, i + 1));
    }
  }

  /// Find the facets of the polyhedron.
  void poly_facet_list( const ConvexImpl *poly, std::vector<vw::Vector<mpq_class> > &points, std::vector<std::vector<unsigned> > &facets ) {
    if (!poly || poly->is_empty())
      return;
      
    unsigned dim = poly->space_dimension();
    unsigned num_points = 0;
    vw::Vector<double> vertexv( dim );

    VW_ASSERT(dim > 0 && dim <= 3, vw::ArgumentErr() << "Cannot find facet list of dimension <= 0 or > 3!");

    poly->get_generators(points);
    num_points = points.size();

    if (dim == 1) {
      VW_ASSERT(num_points <= 2, vw::LogicErr() << "Found line with more than 2 vertices!");
      facets.push_back(std::vector<unsigned>());
      if (num_points == 1)
        facets[0].push_back(0);
      else {
        facets[0].push_back(0);
        facets[0].push_back(1);
      }
    }
    else if (dim == 2 || dim == 3) {
    
#if defined(VW_HAVE_PKG_QHULL) && VW_HAVE_PKG_QHULL==1

      vw::Mutex::Lock lock(convex_qhull_mutex);
      double *p = new double[dim * num_points];
      double **ps = 0;
      facetT *facet;
      vw::Vector<double> facetv( dim + 1 );
      unsigned num_facets;
      vertexT *vertex;
      vertexT **vertexp;
      vw::Vector<double> vertexv2( 2 );
      unsigned *num_vertices = 0;
      unsigned num_vertices_;
      vw::Matrix<double> *basis = 0;
      vw::Vector<double> *plane_origin = 0;
      vw::Vector<double> proj;
      vw::geometry::GeomPrimitive **prims;
      PointPrimitive *prim;
      IndexedEdge edge;
      std::vector<IndexedEdge> edges;
      unsigned edges_used;
      unsigned index;
      unsigned j, k, l;
      bool found_it;
  
      prims = new vw::geometry::GeomPrimitive*[num_points];
      k = 0;
      l = 0;
      for (std::vector<vw::Vector<mpq_class> >::iterator i = points.begin(); i != points.end(); i++, l++) {
        prim = new PointPrimitive;
        prim->index = l;
        unconvert_vector(*i, prim->point);
        prim->bbox.grow(prim->point);
        for (j = 0; j < dim; j++, k++)
          p[k] = prim->point[j];
        prims[l] = (vw::geometry::GeomPrimitive*)prim;
      }
      VW_ASSERT(k == dim * num_points, vw::LogicErr() << "k != dim * num_points!");
      vw::geometry::SpatialTree st( num_points, prims, 5 );
  
      qhull_run(dim, num_points, p);
      if (dim == 2) {
        num_facets = 1;
        ps = new double*[num_facets];
        num_vertices = new unsigned[num_facets];
        basis = new vw::Matrix<double>[num_facets];
        plane_origin = new vw::Vector<double>[num_facets];
        j = 0;
        num_vertices[j] = 0;
        FORALLvertices {
          num_vertices[j]++;
        }
        ps[j] = new double[dim * num_vertices[j]];
        k = 0;
        FORALLvertices {
          for (l = 0; l < dim; l++, k++)
            ps[j][k] = vertex->point[l];
        }
      }
      else {
        num_facets = 0;
        FORALLfacets {
          num_facets++;
        }
        ps = new double*[num_facets];
        num_vertices = new unsigned[num_facets];
        basis = new vw::Matrix<double>[num_facets];
        plane_origin = new vw::Vector<double>[num_facets];
        j = 0;
        FORALLfacets {
          num_vertices[j] = 0;
          FOREACHvertex_(facet->vertices) {
            num_vertices[j]++;
          }
          ps[j] = new double[(dim - 1) * num_vertices[j]];
          for (l = 0; l < dim; l++)
            facetv[l] = -facet->normal[l];
          facetv[l] = -facet->offset;
          gram_schmidt(subvector(facetv, 0, dim), basis[j]);
          facetv /= norm_2(subvector(facetv, 0, dim));
          plane_origin[j] = elem_prod(-facetv[dim], subvector(facetv, 0, dim));
          k = 0;
          FOREACHvertex_(facet->vertices) {
            for (l = 0; l < dim; l++)
              vertexv[l] = vertex->point[l];
            proj = transpose(basis[j]) * vertexv;
            //VW_ASSERT(proj[0] == -facetv[dim], vw::LogicErr() << "Projection to plane failed!");
            for (l = 1; l < dim; l++, k++)
              ps[j][k] = proj[l];
          }
          j++;
        }
      }
      qhull_free();
  
      for (j = 0; j < num_facets; j++) {
        facets.push_back(std::vector<unsigned>());
        qhull_run(2, num_vertices[j], ps[j]);
        edges.clear();
        FORALLfacets {
          num_vertices_ = 0;
          FOREACHvertex_(facet->vertices) {
            num_vertices_++;
          }
          VW_ASSERT(num_vertices_ <= 2, vw::LogicErr() << "Found facet with more than 2 vertices!");
          if (num_vertices_ == 1) {
            vertex = SETfirstt_(facet->vertices, vertexT);
            for (l = 0; l < 2; l++)
              vertexv2[l] = vertex->point[l];
            plane_unproject(dim, vertexv2, basis[j], plane_origin[j], vertexv);
            prim = (PointPrimitive*)st.closest(vertexv);
            VW_ASSERT(prim, vw::LogicErr() << "No closest vertex found!");
            VW_ASSERT(facets[j].empty(), vw::LogicErr() << "Found more than one singleton vertex!");
            facets[j].push_back(prim->index);
          }
          else if (num_vertices_ == 2) {
            vertex = SETfirstt_(facet->vertices, vertexT);
            for (l = 0; l < 2; l++)
              vertexv2[l] = vertex->point[l];
            plane_unproject(dim, vertexv2, basis[j], plane_origin[j], vertexv);
            prim = (PointPrimitive*)st.closest(vertexv);
            VW_ASSERT(prim, vw::LogicErr() << "No closest vertex found!");
            edge.index[0] = prim->index;
            vertex = SETsecondt_(facet->vertices, vertexT);
            for (l = 0; l < 2; l++)
              vertexv2[l] = vertex->point[l];
            plane_unproject(dim, vertexv2, basis[j], plane_origin[j], vertexv);
            prim = (PointPrimitive*)st.closest(vertexv);
            VW_ASSERT(prim, vw::LogicErr() << "No closest vertex found!");
            edge.index[1] = prim->index;
            edge.used = false;
            edges.push_back(edge);
          }
        }
        if (edges.empty()) {
          VW_ASSERT(!facets[j].empty(), vw::LogicErr() << "Found neither a singleton vertex nor an edge!");
        }
        else {
          VW_ASSERT(facets[j].empty(), vw::LogicErr() << "Found both a singleton vertex and an edge!");
          facets[j].push_back(edges[0].index[0]);
          index = edges[0].index[1];
          edges[0].used = true;
          edges_used = 1;
          while (edges_used < edges.size()) {
            facets[j].push_back(index);
            found_it = false;
            for (k = 1; k < edges.size(); k++) {
              if (!edges[k].used) {
                if (edges[k].index[0] == index) {
                  index = edges[k].index[1];
                  found_it = true;
                }
                else if (edges[k].index[1] == index) {
                  index = edges[k].index[0];
                  found_it = true;
                }
                if (found_it) {
                  edges[k].used = true;
                  edges_used++;
                  break;
                }
              }
            }
            VW_ASSERT(found_it, vw::LogicErr() << "Polygon is not closed!");
          }
          VW_ASSERT(index == edges[0].index[0], vw::LogicErr() << "Polygon is not closed!");
        }
        qhull_free();
      }
  
      delete[] p;
      p = 0;
      for (j = 0; j < num_facets; j++)
        delete[] ps[j];
      delete[] ps;
      ps = 0;
      for (j = 0; j < num_points; j++)
        delete (PointPrimitive*)prims[j];
      delete[] prims;
      prims = 0;
      delete[] num_vertices;
      num_vertices = 0;
      delete[] basis;
      basis = 0;
      delete[] plane_origin;
      plane_origin = 0;
      
#else // defined(VW_HAVE_PKG_QHULL) && VW_HAVE_PKG_QHULL==1
      VW_ASSERT(0, vw::ArgumentErr() << "Must have qhull to find facet list for dimension >= 2!");
#endif // defined(VW_HAVE_PKG_QHULL) && VW_HAVE_PKG_QHULL==1

    }
  }

  /// Offsets the polyhedron by the given vector.
  void offset_poly( vw::Vector<mpq_class> const& v, ConvexImpl *&poly ) {
    unsigned dim = poly->space_dimension();
    std::vector<vw::Vector<mpq_class> > gs;
    poly->get_generators(gs);
    for (std::vector<vw::Vector<mpq_class> >::iterator i = gs.begin(); i != gs.end(); i++) {
      for (unsigned j = 0; j < (*i).size(); j++)
        (*i)[j] += v[j];
    }
    delete poly;
    poly = new ConvexImpl( dim );
    poly->add_generators(gs);
  }

  /// Scales the polyhedron relative to the origin.
  void scale_poly( mpq_class const& s, ConvexImpl *&poly ) {
    unsigned dim = poly->space_dimension();
    std::vector<vw::Vector<mpq_class> > gs;
    poly->get_generators(gs);
    for (std::vector<vw::Vector<mpq_class> >::iterator i = gs.begin(); i != gs.end(); i++) {
      for (unsigned j = 0; j < (*i).size(); j++)
        (*i)[j] *= s;
    }
    delete poly;
    poly = new ConvexImpl( dim );
    poly->add_generators(gs);
  }

} // namespace

namespace vw {
namespace geometry {

  namespace convex_promote {

    std::ostream& operator<<( std::ostream& os, Promoted const& r ) {
      if (!r.is_integral)
        return os << r.val.f;
      if (r.is_signed)
        return os << r.val.s;
      return os << r.val.u;
    }

  } // namespace convex_promote

  /*static*/ void *Convex::new_poly( unsigned dim ) {
    ConvexImpl *p = new ConvexImpl( dim );
    return (void*)p;
  }

  /*static*/ void *Convex::new_poly( const void *poly ) {
    ConvexImpl *p = new ConvexImpl( *((const ConvexImpl*)poly) );
    return (void*)p;
  }

  /*static*/ void Convex::new_poly_if_needed( unsigned dim, void *&poly ) {
    if (!poly)
      poly = new_poly(dim);
  }
  
  /*static*/ void Convex::delete_poly( void *poly ) {
    if (poly)
      delete (ConvexImpl*)poly;
  }
  
  /*static*/ void Convex::copy_poly( const void *from_poly, void *&to_poly ) {
    if (to_poly)
      *((ConvexImpl*)to_poly) = *((const ConvexImpl*)from_poly);
    else
      to_poly = new_poly(from_poly);
  }
  
  /*static*/ bool Convex::have_qhull() {
#if defined(VW_HAVE_PKG_QHULL) && VW_HAVE_PKG_QHULL==1
    return true;
#else
    return false;
#endif
  }

  void Convex::init_with_qhull( unsigned dim, unsigned num_points, double *p ) {
#if defined(VW_HAVE_PKG_QHULL) && VW_HAVE_PKG_QHULL==1
    Mutex::Lock lock(convex_qhull_mutex);
    facetT *facet;
    Vector<double> facetv(dim + 1);
    Vector<mpq_class> facetq(dim + 1);
    unsigned i;
    unsigned num_facets = 0, num_vertices = 0;
    vertexT *vertex;
    Vector<double> vertexv(dim);
    Vector<mpq_class> vertexq(dim);

    qhull_run(dim, num_points, p);
    
#if 0
    m_poly = (void*)(new ConvexImpl( dim, true ));
    FORALLfacets {
      for (i = 0; i < dim; i++)
        facetv[i] = -facet->normal[i];
      facetv[i] = -facet->offset;
      //NOTE: Printing facet->toporient here for the [0,1]^3 unit cube test case (see TestConvex.h) demonstrates that facet->toporient is meaningless in the qhull output.
      ((ConvexImpl*)m_poly)->add_constraint(convert_vector(facetv, facetq));
      num_facets++;
    }
    FORALLvertices {
      num_vertices++;
    }
#else
    //NOTE: This seems like a ridiculous thing to do, using qhull to find the convex
    // hull and then making PPL compute the convex hull again. However, PPL is only 
    // computing the convex hull of the points that are actually on the convex hull 
    // (as determined by qhull), so we don't run into the extremely long runtime that
    // we would have run into by having PPL take the initial convex hull (qhull is 
    // much faster with a large number of points). Also, as it turns out, 
    // transferring the hyperplanes from qhull to PPL results in a lot more numerical 
    // error than transferring the vertices.
    m_poly = new_poly(dim);
    FORALLfacets {
      num_facets++;
    }
    FORALLvertices {
      for (i = 0; i < dim; i++)
        vertexv[i] = vertex->point[i];
      ((ConvexImpl*)m_poly)->add_generator(convert_vector(vertexv, vertexq));
      num_vertices++;
    }
#endif
    //std::cout << "Qhull: " << num_facets << " facets and " << num_vertices << " vertices" << std::endl;
    //std::cout << "PPL: " << this->num_facets() << " facets and " << this->num_vertices() << " vertices" << std::endl;
    
    VW_ASSERT(!this->empty(), LogicErr() << "Convex hull is empty!");
    VW_ASSERT(!((const ConvexImpl*)m_poly)->is_universe(), LogicErr() << "Convex hull is universe!");
    
    qhull_free();
#else // defined(VW_HAVE_PKG_QHULL) && VW_HAVE_PKG_QHULL==1
    return;
#endif // defined(VW_HAVE_PKG_QHULL) && VW_HAVE_PKG_QHULL==1
  }
  
  void Convex::grow_( Vector<Promoted> const& point ) {
    Vector<mpq_class> pointq;
    ((ConvexImpl*)m_poly)->add_generator(convert_vector(point, pointq));
  }

  void Convex::grow( Convex const& bconv ) {
    new_poly_if_needed(((const ConvexImpl*)(bconv.poly()))->space_dimension(), m_poly);
    ((ConvexImpl*)m_poly)->poly_hull_assign(*((const ConvexImpl*)(bconv.poly())));
  }

  void Convex::crop( Convex const& bconv ) {
    if (!m_poly)
      return;
    ((ConvexImpl*)m_poly)->intersection_assign(*((const ConvexImpl*)(bconv.poly())));
  }

  void Convex::expand( double offset ) {
    VW_ASSERT(!empty(), LogicErr() << "Cannot expand an empty polyhedron!");
    unsigned dim = ((const ConvexImpl*)m_poly)->space_dimension();
    Vector<mpq_class> center_( dim );
    Vector<mpq_class> d;
    double norm;
    std::vector<vw::Vector<mpq_class> > gs;
    ((const ConvexImpl*)m_poly)->get_generators(gs);
    poly_center(gs, center_);
    for (std::vector<vw::Vector<mpq_class> >::iterator i = gs.begin(); i != gs.end(); i++) {
      d = *i - center_;
      norm = norm_2_gmp(d);
      if (offset < 0.0 && -offset >= norm)
        *i = center_;
      else
        *i = center_ + (1.0 + offset / norm) * d;
    }
    delete_poly(m_poly);
    m_poly = new_poly( dim );
    ((ConvexImpl*)m_poly)->add_generators(gs);
  }

  bool Convex::contains_( Vector<Promoted> const& point ) const {
    Vector<mpq_class> pointq;
    return ((const ConvexImpl*)m_poly)->contains(convert_vector(point, pointq));
  }

  bool Convex::contains( Convex const& bconv ) const {
    if (!m_poly)
      return false;
    return ((const ConvexImpl*)m_poly)->contains(*((const ConvexImpl*)(bconv.poly())));
  }

  bool Convex::intersects( Convex const& bconv ) const {
    if (!m_poly)
      return false;
    return !((const ConvexImpl*)m_poly)->is_disjoint_from(*((const ConvexImpl*)(bconv.poly())));
  }

  double Convex::size() const {
    if (!m_poly)
      return 0.0;
    unsigned dim = ((const ConvexImpl*)m_poly)->space_dimension();
    double result = 0;
    double d;
    Vector<mpq_class> c;
    std::vector<vw::Vector<mpq_class> > gs;
    ((const ConvexImpl*)m_poly)->get_generators(gs);
    poly_center(gs, c);
    for (std::vector<vw::Vector<mpq_class> >::iterator i = gs.begin(); i != gs.end(); i++) {
      d = norm_2_sqr_gmp(*i - c);
      result = std::max(result, d);
    }
    result = 2*std::sqrt(result);
    return result;
  }

  Vector<double> Convex::center() const {
    VW_ASSERT(!empty(), LogicErr() << "Cannot find the center of an empty polyhedron!");
    unsigned dim = ((const ConvexImpl*)m_poly)->space_dimension();
    Vector<mpq_class> c(dim);
    Vector<double> cd(dim);
    std::vector<vw::Vector<mpq_class> > gs;
    ((const ConvexImpl*)m_poly)->get_generators(gs);
    poly_center(gs, c);
    unconvert_vector(c, cd);
    return cd;
  }

  bool Convex::equal( Convex const& bconv ) const {
    if (empty())
      return bconv.empty();
    return ((const ConvexImpl*)m_poly)->equal(*((const ConvexImpl*)(bconv.poly())));
  }

  bool Convex::empty() const {
    if (!m_poly)
      return true;
    return ((const ConvexImpl*)m_poly)->is_empty();
  }

  void Convex::print( std::ostream& os ) const {
    if (!m_poly) {
      os << "false";
      return;
    }
    ((const ConvexImpl*)m_poly)->print(os);
  }

  void Convex::write( std::ostream& os, bool binary /* = false*/ ) const {
    if (!empty()) {
      unsigned dim = ((const ConvexImpl*)m_poly)->space_dimension();
      std::vector<Vector<double> > points;
      Vector<double> vd(dim);
      std::vector<vw::Vector<mpq_class> > gs;
      ((const ConvexImpl*)m_poly)->get_generators(gs);
      for (std::vector<vw::Vector<mpq_class> >::iterator i = gs.begin(); i != gs.end(); i++) {
        unconvert_vector(*i, vd);
        points.push_back(vd);
      }
      write_point_list(os, points, binary);
    }
  }

  void Convex::write( const char *fn, bool binary /* = false*/ ) const {
    if (binary) {
      std::ofstream of(fn, std::ofstream::out | std::ofstream::binary);
      write(of, binary);
      of.close();
    }
    else {
      std::ofstream of(fn);
      write(of, binary);
      of.close();
    }
  }

  void Convex::write_vrml( std::ostream& os ) const {
    os << "#VRML V1.0 ascii\n\n";
    os << "# Created by the Intelligent Robotics Group,\n";
    os << "# NASA Ames Research Center\n";
    os << "# File generated by the NASA Ames Vision Workbench.\n\n";
    os << "Separator {\n";
  
    if (!empty()) {
      unsigned dim = ((const ConvexImpl*)m_poly)->space_dimension();
      unsigned num_points;
      unsigned num_facets;
      Vector<double> vertexv( dim );
      std::vector<Vector<mpq_class> > points;
      std::vector<std::vector<unsigned> > facets;
      unsigned i, j, k;
      
      poly_facet_list((const ConvexImpl*)m_poly, points, facets);
      num_points = points.size();
      num_facets = facets.size();
      
      os << "   Coordinate3 {\n";
      os << "      point [\n";
      for (i = 0; i < num_points; i++) {
        unconvert_vector(points[i], vertexv);
        os << "       ";
        for (k = 0; k < dim; k++)
          os << " " << vertexv[k];
        for (; k < 3; k++)
          os << " 0";
        os << ",\n";
      }
      os << "      ]\n";
      os << "   }\n";
      
      os << "   Material {\n";
      os << "      ambientColor 1 1 1\n";
      os << "      diffuseColor 1 1 1\n";
      os << "      specularColor 0.00 0.00 0.00\n";
      os << "      emissiveColor 0.00 0.00 0.00\n";
      os << "      shininess 0.00\n";
      os << "      transparency 0.00\n";
      os << "   }\n";
      os << "   IndexedFaceSet {\n";
      os << "      coordIndex [\n";
      os << "      ";
      for (i = 0; i < num_facets; i++) {
        os << "   " << facets[i][0] << ",";
        for (j = 1; j < facets[i].size(); j++)
          os << " " << facets[i][j] << ",";
        os << " " << facets[i][0] << ",";
        os << " -1,\n";
        os << "      ";
      }
      os << "]\n";
      os << "   }\n";
      
      os << "   Material {\n";
      os << "      ambientColor 0 0 1\n";
      os << "      diffuseColor 0 0 1\n";
      os << "      specularColor 0.00 0.00 0.00\n";
      os << "      emissiveColor 0.00 0.00 0.00\n";
      os << "      shininess 0.00\n";
      os << "      transparency 0.00\n";
      os << "   }\n";
      os << "   IndexedLineSet {\n";
      os << "      coordIndex [\n";
      os << "      ";
      for (i = 0; i < num_facets; i++) {
        os << "   " << facets[i][0] << ",";
        for (j = 1; j < facets[i].size(); j++)
          os << " " << facets[i][j] << ",";
        os << " " << facets[i][0] << ",";
        os << " -1,\n";
        os << "      ";
      }
      os << "]\n";
      os << "   }\n";
    }
    
    os << "}\n";
  }

  void Convex::write_vrml( const char *fn ) const {
    std::ofstream of(fn);
    write_vrml(of);
  }
  
  void Convex::write_oogl( std::ostream& os ) const {
    os << "OFF\n\n";
    os << "# Created by the Intelligent Robotics Group,\n";
    os << "# NASA Ames Research Center\n";
    os << "# File generated by the NASA Ames Vision Workbench.\n\n";
  
    if (empty()) {
      os << "0 0 0\n";
    }
    else {
      unsigned dim = ((const ConvexImpl*)m_poly)->space_dimension();
      unsigned num_points;
      unsigned num_facets;
      unsigned num_edges;
      Vector<double> vertexv( dim );
      std::vector<Vector<mpq_class> > points;
      std::vector<std::vector<unsigned> > facets;
      unsigned i, j, k;
      
      poly_facet_list((const ConvexImpl*)m_poly, points, facets);
      num_points = points.size();
      num_facets = facets.size();
      
      num_edges = 0;
      for (i = 0; i < num_facets; i++) {
        if (facets[i].size() > 1)
          num_edges += facets[i].size();
      }
      num_edges /= 2;
      
      os << num_points << " " << num_facets << " " << num_edges << "\n\n";
      
      for (i = 0; i < num_points; i++) {
        unconvert_vector(points[i], vertexv);
        for (k = 0; k < dim; k++)
          os << vertexv[k] << " ";
        for (; k < 3; k++)
          os << "0 ";
        os << "\n";
      }
      os << "\n";
      
      for (i = 0; i < num_facets; i++) {
        os << facets[i].size();
        for (j = 0; j < facets[i].size(); j++)
          os << " " << facets[i][j];
        os << " 1 1 1 1\n";
      }
    }
  }

  void Convex::write_oogl( const char *fn ) const {
    std::ofstream of(fn);
    write_oogl(of);
  }
  
  unsigned Convex::num_facets() const {
    if (!m_poly)
      return 0;
    return ((const ConvexImpl*)m_poly)->num_constraints();
  }

  unsigned Convex::num_vertices() const {
    if (!m_poly)
      return 0;
    return ((const ConvexImpl*)m_poly)->num_generators();
  }
  
  BoxN Convex::bounding_box() const {
    BoxN bbox;
    if (!m_poly)
      return bbox;
    unsigned dim = ((const ConvexImpl*)m_poly)->space_dimension();
    Vector<double> vd(dim);
    std::vector<vw::Vector<mpq_class> > gs;
    ((const ConvexImpl*)m_poly)->get_generators(gs);
    for (std::vector<vw::Vector<mpq_class> >::iterator i = gs.begin(); i != gs.end(); i++) {
      unconvert_vector(*i, vd);
      bbox.grow(vd);
    }
    return bbox;
  }

  void Convex::operator_mult_eq_( Promoted const& s ) {
    mpq_class q;
    ConvexImpl *poly = (ConvexImpl*)m_poly;
    convert_scalar(s, q);
    scale_poly(q, poly);
    m_poly = poly;
  }

  void Convex::operator_div_eq_( Promoted const& s ) {
    mpq_class q;
    ConvexImpl *poly = (ConvexImpl*)m_poly;
    convert_scalar(s, q);
    scale_poly(mpq_class(q.get_den(), q.get_num()), poly);
    m_poly = poly;
  }

  void Convex::operator_plus_eq_( Vector<Promoted> const& v ) {
    unsigned dim = ((const ConvexImpl*)m_poly)->space_dimension();
    Vector<mpq_class> vq( dim );
    ConvexImpl *poly = (ConvexImpl*)m_poly;
    convert_vector(v, vq);
    offset_poly(vq, poly);
    m_poly = poly;
  }

  void Convex::operator_minus_eq_( Vector<Promoted> const& v ) {
    unsigned dim = ((const ConvexImpl*)m_poly)->space_dimension();
    Vector<mpq_class> vq( dim );
    ConvexImpl *poly = (ConvexImpl*)m_poly;
    convert_vector(v, vq);
    offset_poly(-vq, poly);
    m_poly = poly;
  }

  std::ostream& operator<<( std::ostream& os, Convex const& bconv ) {
    bconv.print(os);
    return os;
  }

  void write_convex( std::string const& filename, Convex const& bconv, bool binary /* = false*/ ) {
    bconv.write(filename.c_str(), binary);
  }

  void read_convex( std::string const& filename, Convex& bconv, bool binary /* = false*/ ) {
    std::vector<Vector<double> > points;
    read_point_list(filename, points, binary);
    bconv = Convex(points);
  }

}} // namespace vw::geometry