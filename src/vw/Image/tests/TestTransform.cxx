// __BEGIN_LICENSE__
// Copyright (C) 2006-2010 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__

// TestTransform.h
#include <gtest/gtest.h>

#include <vw/Image/ImageView.h>
#include <vw/Image/Transform.h>

using namespace vw;

template <template<class> class TraitT, class T>
static bool bool_trait( T const& /*arg*/ ) {
  return TraitT<T>::value;
}

TEST( Transform, BBoxComp ) { // wikka wikka
  ImageView<double> im(2,3); im(0,0)=1; im(1,0)=2; im(0,1)=3; im(1,1)=4; im(0,2)=5; im(1,2)=6;
  TransformView<InterpolationView<EdgeExtensionView<ImageView<double>, ZeroEdgeExtension>, BilinearInterpolation>, TranslateTransform> im2 = transform(im, TranslateTransform(1,1));
}

TEST( Transform, Translate ) {
  ImageView<double> im(2,3); im(0,0)=1; im(1,0)=2; im(0,1)=3; im(1,1)=4; im(0,2)=5; im(1,2)=6;
  ImageView<double> im3 = transform(im, TranslateTransform(1,1));
  TransformView<InterpolationView<EdgeExtensionView<ImageView<double>, ZeroEdgeExtension>, BilinearInterpolation>, TranslateTransform> im2 = transform(im, TranslateTransform(1,1));
  ASSERT_EQ( im2.cols(), 2 );
  ASSERT_EQ( im2.rows(), 3 );
  EXPECT_EQ( im2(1,1), 1 );
  EXPECT_EQ( im2(0,0), 0 );
  EXPECT_EQ( im2(1,2), 3 );
  EXPECT_EQ( im2(-1,-1), 0 );
  EXPECT_EQ( im2(-100,-100), 0 );

  // Test accessor
  EXPECT_EQ( *(im2.origin().advance(1,1)), 1 );
  EXPECT_EQ( *(im2.origin().advance(-1,-1)), 0 );

  // Test the traits
  ASSERT_TRUE( bool_trait<IsFloatingPointIndexable>( im2 ) );
  ASSERT_TRUE( bool_trait<IsFloatingPointIndexable>( transform(im, TranslateTransform(1,1)) ) );
  ASSERT_FALSE( bool_trait<IsMultiplyAccessible>( transform(im, TranslateTransform(1,1)) ) );
}

TEST( Transform, TranslateFunc ) {
  ImageView<double> im(2,3); im(0,0)=1; im(1,0)=2; im(0,1)=3; im(1,1)=4; im(0,2)=5; im(1,2)=6;
  ImageView<double> im2;
  // FIXME This test exhibits a Heisenbug on gc.cs.cmu.edu when used
  // with the default BilinearInterpolation (Red Hat gcc 4.0.2-8).
  // Attempts at replicating the bug in other contexts fail, so we
  // just work around it here for the moment.
  im2 = translate(im,1.0,1.0,ZeroEdgeExtension(),NearestPixelInterpolation());
  ASSERT_EQ( im2.cols(), 2 );
  ASSERT_EQ( im2.rows(), 3 );
  EXPECT_EQ( im2(1,1), 1 );
  EXPECT_EQ( im2(0,0), 0 );
  EXPECT_EQ( im2(1,2), 3 );
  im2 = translate(im,1,1);
  ASSERT_EQ( im2.cols(), 2 );
  ASSERT_EQ( im2.rows(), 3 );
  EXPECT_EQ( im2(1,1), 1 );
  EXPECT_EQ( im2(0,0), 0 );
  EXPECT_EQ( im2(1,2), 3 );
}

TEST( Transform, Resample ) {
  ImageView<double> im(2,3); im(0,0)=1; im(1,0)=2; im(0,1)=3; im(1,1)=4; im(0,2)=5; im(1,2)=6;
  TransformView<InterpolationView<EdgeExtensionView<ImageView<double>, ConstantEdgeExtension>, BilinearInterpolation>, ResampleTransform> im2 = resample(im, 2, 2);
  ASSERT_EQ( im2.cols(), 4 );
  ASSERT_EQ( im2.rows(), 6 );
  EXPECT_EQ( im2(0,0), 1 );
  EXPECT_EQ( im2(2,2), 4 );
  EXPECT_EQ( im2(1,1), 2.5 );
  EXPECT_EQ( im2(-1,-1), 1 );
  EXPECT_EQ( im2(-100,-100), 1 );
  EXPECT_EQ( im2(100,100), 6 );

  // Test accessor
  EXPECT_EQ( *(im2.origin().advance(0,0)), 1 );
  EXPECT_EQ( *(im2.origin().advance(-1,-1)), 1 );

  // Test the traits
  ASSERT_TRUE( bool_trait<IsFloatingPointIndexable>( im2 ) );
  ASSERT_TRUE( bool_trait<IsFloatingPointIndexable>( transform(im, ResampleTransform(1,1)) ) );
  ASSERT_FALSE( bool_trait<IsMultiplyAccessible>( transform(im, ResampleTransform(1,1)) ) );
}
