//===- ShapeToShapeLowering.cpp - Prepare for lowering to Standard --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PassDetail.h"
#include "mlir/Dialect/Shape/IR/Shape.h"
#include "mlir/Dialect/Shape/Transforms/Passes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
using namespace mlir::shape;

namespace {
/// Converts `shape.num_elements` to `shape.reduce`.
struct NumElementsOpConverter : public OpRewritePattern<NumElementsOp> {
public:
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(NumElementsOp op,
                                PatternRewriter &rewriter) const final;
};
} // namespace

LogicalResult
NumElementsOpConverter::matchAndRewrite(NumElementsOp op,
                                        PatternRewriter &rewriter) const {
  auto loc = op.getLoc();
  Value init = rewriter.create<ConstSizeOp>(loc, rewriter.getIndexAttr(1));
  ReduceOp reduce = rewriter.create<ReduceOp>(loc, op.shape(), init);

  // Generate reduce operator.
  Block *body = reduce.getBody();
  OpBuilder b = OpBuilder::atBlockEnd(body);
  Value product = b.create<MulOp>(loc, b.getType<SizeType>(),
                                  body->getArgument(1), body->getArgument(2));
  b.create<YieldOp>(loc, product);

  rewriter.replaceOp(op, reduce.result());
  return success();
}

namespace {
struct ShapeToShapeLowering
    : public ShapeToShapeLoweringBase<ShapeToShapeLowering> {
  void runOnFunction() override;
};
} // namespace

void ShapeToShapeLowering::runOnFunction() {
  MLIRContext &ctx = getContext();

  OwningRewritePatternList patterns;
  populateShapeRewritePatterns(&ctx, patterns);

  ConversionTarget target(getContext());
  target.addLegalDialect<ShapeDialect>();
  target.addIllegalOp<NumElementsOp>();
  if (failed(mlir::applyPartialConversion(getFunction(), target, patterns)))
    signalPassFailure();
}

void mlir::populateShapeRewritePatterns(MLIRContext *context,
                                        OwningRewritePatternList &patterns) {
  patterns.insert<NumElementsOpConverter>(context);
}

std::unique_ptr<Pass> mlir::createShapeToShapeLowering() {
  return std::make_unique<ShapeToShapeLowering>();
}
