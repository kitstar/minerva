#include "op/impl/cuda.h"
#include "op/impl/cuda/cuda_perform.h"
#include "op/context.h"
#include "op/closure.h"
#include <glog/logging.h>
#ifdef HAS_CUDA
#include <cuda_runtime.h>
#endif

namespace minerva {
#ifdef HAS_CUDA
namespace cuda {

void Arithmetic(const DataList& inputs, const DataList& outputs, ArithmeticClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 2) << "Arithmetic takes 2 inputs";
  CHECK_EQ(outputs.size(), 1) << "Arithmetic takes 1 output";
  float* left = inputs[0].data();
  float* right = inputs[1].data();
  float* res = outputs[0].data();
  size_t size = outputs[0].size().Prod();
  int m = inputs[0].size()[0];
  int n = inputs[0].size()[1];
  switch (closure.type) {
    case ArithmeticType::kAdd:
      CudaPerformAdd(left, right, res, m, n, context.cublas_handle);
      break;
    case ArithmeticType::kSub:
      CudaPerformSub(left, right, res, m, n, context.cublas_handle);
      break;
    case ArithmeticType::kMult:
      CudaPerformDotMult(left, right, res, size, context.stream);
      break;
    case ArithmeticType::kDiv:
      abort();
      CudaPerformDotDiv(res, left, right, size, context.stream);
      break;
  }
}

void MatMult(const DataList& inputs, const DataList& outputs, MatMultClosure& closure, const CudaRuntimeContext & context) {
  CHECK_EQ(inputs.size(), 2) << "(matmult) #inputs is wrong!";
  CHECK_EQ(outputs.size(), 1) << "(matmult) #outputs is wrong!";
  float* left_data = inputs[0].data();
  float* right_data = inputs[1].data();
  float* res_data = outputs[0].data();
  int m = inputs[0].size()[0];
  int k = inputs[0].size()[1];
  int n = outputs[0].size()[1];
  // ATTENTION: the data is column major !!
  CudaPerformMatMult(left_data, right_data, res_data, m, n, k, context.cublas_handle);
}

void ArithmeticConst(const DataList& inputs, const DataList& outputs,
  ArithmeticConstClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 1) << "(arithmetic const) #inputs is wrong!";
  CHECK_EQ(outputs.size(), 1) << "(arithmetic const) #outputs is wrong!";
  float val = closure.val;
  float* in_data = inputs[0].data();
  float* res_data = outputs[0].data();
  int m = inputs[0].size()[0];
  int n = inputs[0].size()[1];
  size_t size = inputs[0].size().Prod();
  switch (closure.type) {
    case ArithmeticType::kAdd:
      abort();
      break;
    case ArithmeticType::kSub:
      if (closure.side == 0)  // const on left
      {
        CudaPerformLeftConstSub(in_data, res_data, val, size, context.stream);
      }
      else
      {
        CHECK(false) << "we support const on left only";
      }
      break;
    case ArithmeticType::kMult:
      CudaPerformScale(in_data, res_data, m, n, val, context.cublas_handle);
      break;
    case ArithmeticType::kDiv:
      if (closure.side == 0) {// const on left
        CudaPerformLeftConstDiv(in_data, res_data, val, size, context.stream);
      }
      else {// const on right
        CudaPerformScale(in_data, res_data, m, n, 1 / val, context.cublas_handle);
      }
      break;
  }
}

void Transpose(const DataList& inputs, const DataList& outputs,
  TransposeClosure& closure, const CudaRuntimeContext& context) {
  float* in_data = inputs[0].data();
  float* res_data = outputs[0].data();
  int m = inputs[0].size()[0];
  int n = inputs[0].size()[1];
  CudaPerformTranspose(in_data, res_data, m, n, context.cublas_handle);
}

void NormArithmetic(const DataList& inputs, const DataList& outputs, NormArithmeticClosure& closure,
  const CudaRuntimeContext & context) {
  CHECK_EQ(inputs.size(), 2) << "NormArithmetic kernel wrong #input";
  CHECK_EQ(outputs.size(), 1) << "NormArithmetic kernel wrong #output";
  // Normalizee is the chunk with full size, normalizer is the chunk with reduced dimensions
  auto normalizee_size = inputs[0].size();
  auto normalizer_size = inputs[1].size();

  CHECK_EQ(normalizee_size, outputs[0].size()) << "NormArithmetic kernel output size mismatch";
  for (size_t i = 0; i < normalizee_size.NumDims(); ++i) {
    if (normalizer_size[i] != 1 && normalizer_size[i] != normalizee_size[i]) {
      CHECK(false) << "NormArithmetic kernel size mismatch";
    }
  }

  auto normalizee_data = inputs[0].data();
  auto normalizer_data = inputs[1].data();
  auto res_data = outputs[0].data();
  // TODO: support other types of norm op
  CHECK(normalizee_size.NumDims() == 2) << "currently support 2D normalizee matrix only, got "
    << normalizee_size.NumDims();
  CHECK(normalizer_size.NumDims() == 2) << "currently support 2D normalizer matrix only, got "
    << normalizer_size.NumDims();

  int m = normalizee_size[0];
  int n = normalizee_size[1];
  if (normalizer_size[0] == 1) {
    CHECK_EQ(normalizee_size[1], normalizer_size[1]) << "we can only do norm on one dimmension";
    switch(closure.type) {
      case ArithmeticType::kAdd:
        CudaPerformNormAddOnCol(normalizee_data, normalizer_data, res_data, m, n, context.stream);
        break;
      case ArithmeticType::kSub:
        CudaPerformNormSubOnCol(normalizee_data, normalizer_data, res_data, m, n, context.stream);
        break;
      case ArithmeticType::kMult:
        CudaPerformNormMultOnCol(normalizee_data, normalizer_data, res_data, m, n, context.stream);
        break;
      case ArithmeticType::kDiv:
        CudaPerformNormDivOnCol(normalizee_data, normalizer_data, res_data, m, n, context.stream);
        break;
    }
  } else if (normalizer_size[1] == 1) {
    CHECK_EQ(normalizee_size[0], normalizer_size[0]) << "we can only do norm on one dimmension";
    switch(closure.type) {
      case ArithmeticType::kAdd:
        CudaPerformNormAddOnRow(normalizee_data, normalizer_data, res_data, m, n, context.stream);
        break;
      case ArithmeticType::kSub:
        CudaPerformNormSubOnRow(normalizee_data, normalizer_data, res_data, m, n, context.stream);
        break;
      case ArithmeticType::kMult:
        CudaPerformNormMultOnRow(normalizee_data, normalizer_data, res_data, m, n, context.stream);
        break;
      case ArithmeticType::kDiv:
        CudaPerformNormDivOnRow(normalizee_data, normalizer_data, res_data, m, n, context.stream);
        break;
    }
  } else {
    CHECK(false) << "both two dimensions of normalizer are not 1";
  }
}

void Reduction(const DataList& inputs, const DataList& outputs,
  ReductionClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 1) << "Reduction kernel wrong #input";
  CHECK_EQ(outputs.size(), 1) << "Reduction kernel wrong #output";
  auto in_size = inputs[0].size();
  auto out_size = outputs[0].size();

  for (size_t i = 0; i < in_size.NumDims(); ++i) {
    if (out_size[i] != 1 && out_size[i] != in_size[i]) {
      CHECK(false) << "Reduction kernel size mismatch";
    }
  }

  auto in_data = inputs[0].data();
  auto out_data = outputs[0].data();

  // TODO: support other types of norm op
  CHECK_EQ(in_size.NumDims(), 2) << "currently support 2D reduction matrix only";
  CHECK_EQ(out_size.NumDims(), 2) << "currently support 2D reduction matrix only";

  int m = in_size[0];
  int n = in_size[1];
  if (out_size[0] == 1) {
    CHECK_EQ(in_size[1], out_size[1]) << "we can only do reduction on one dimmension";
    switch (closure.type) {
      case ReductionType::kSum:
        CudaPerformReductionSumOnCol(in_data, out_data, m, n, context.stream);
        break;
      case ReductionType::kMax:
        CudaPerformReductionMaxOnCol(in_data, out_data, m, n, context.stream);
        break;
    }
  } else if (out_size[1] == 1) {
    CHECK_EQ(in_size[0], out_size[0]) << "we can only do reduction on one dimmension";
    switch (closure.type) {
      case ReductionType::kSum:
        CudaPerformReductionSumOnRow(in_data, out_data, m, n, context.stream);
        break;
      case ReductionType::kMax:
        CudaPerformReductionMaxOnRow(in_data, out_data, m, n, context.stream);
        break;
    }
  } else {
    CHECK(false) << "both two dimensions of reduction are not 1";
  }
}

void MaxIndex(const DataList& inputs, const DataList& outputs,
  MaxIndexClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 1) << "MaxIndex kernel wrong #input";
  CHECK_EQ(outputs.size(), 1) << "MaxIndex kernel wrong #output";
  auto in_size = inputs[0].size();
  auto out_size = outputs[0].size();

  for (size_t i = 0; i < in_size.NumDims(); ++i) {
    if (out_size[i] != 1 && out_size[i] != in_size[i]) {
      CHECK(false) << "MaxIndex kernel size mismatch";
    }
  }

  auto in_data = inputs[0].data();
  auto out_data = outputs[0].data();
  // TODO: support other types of norm op
  CHECK_EQ(in_size.NumDims(), 2) << "currently support 2D MaxIndex matrix only";
  CHECK_EQ(out_size.NumDims(), 2) << "currently support 2D MaxIndex matrix only";
  CHECK(closure.dim == 0 || closure.dim == 1)
    << "currently support MaxIndex on first or second dim only";

  int m = in_size[0];
  int n = in_size[1];
  if (out_size[0] == 1) {
    CHECK_EQ(in_size[1], out_size[1]) << "we can only do MaxIndex on one dimmension";
    CudaPerformMaxIndexOnCol(in_data, out_data, m, n, context.stream);
  } else if (out_size[1] == 1) {
    CHECK_EQ(in_size[0], out_size[0]) << "we can only do MaxIndex on one dimmension";
    CudaPerformMaxIndexOnRow(in_data, out_data, m, n, context.stream);
  } else {
    CHECK(false) << "both two dimensions of normalizer are not 1";
  }
}

void Elewise(const DataList& inputs, const DataList& outputs, ElewiseClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 1) << "(elewise) #inputs is wrong!";
  CHECK_EQ(outputs.size(), 1) << "(elewise) #outputs is wrong!";
  float* in_data = inputs[0].data();
  float* res_data = outputs[0].data();
  int length = outputs[0].size().Prod();
  switch (closure.type) {
    case ElewiseType::kExp:
      CudaPerformElewiseExp(in_data, res_data, length, context.stream);
      break;
    case ElewiseType::kLn:
      CudaPerformElewiseLn(in_data, res_data, length, context.stream);
      break;
    case ElewiseType::kSigmoid:
      CudaPerformElewiseSigmoid(in_data, res_data, length, context.stream);
      break;
    case ElewiseType::kNegative:
      CudaPerformElewiseNegative(in_data, res_data, length, context.stream);
      break;
  }
}

void ConvForward(const DataList& inputs, const DataList& outputs, ConvForwardClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 3) << "(conv forward) #inputs wrong";
  CHECK_EQ(outputs.size(), 1) << "(conv forward) #outputs wrong";
  auto& bottom = inputs[0];
  auto& filter = inputs[1];
  auto& bias = inputs[2];
  auto& top = outputs[0];
  int num_images = bottom.size()[3];
  int bottom_num_channels = bottom.size()[2];
  int top_num_channels = top.size()[2];
  int bottom_height = bottom.size()[1];
  int bottom_width = bottom.size()[0];
  int filter_height = filter.size()[1];
  int filter_width = filter.size()[0];
  CudaPerformConvForward(bottom.data(), filter.data(), bias.data(), top.data(), num_images, bottom_num_channels, top_num_channels, bottom_height, bottom_width, closure.pad_height, closure.pad_width, closure.stride_vertical, closure.stride_horizontal, filter_height, filter_width, context.stream, context.cudnn_handle);
}

void ConvBackwardData(const DataList& inputs, const DataList& outputs, ConvBackwardDataClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 2) << "(conv backward data) #inputs wrong";
  CHECK_EQ(outputs.size(), 1) << "(conv backward data) #outputs wrong";
  auto& top_diff = inputs[0];
  auto& filter = inputs[1];
  auto& bottom_diff = outputs[0];
  int num_images = top_diff.size()[3];
  int bottom_num_channels = bottom_diff.size()[2];
  int top_num_channels = top_diff.size()[2];
  int top_height = top_diff.size()[1];
  int top_width = top_diff.size()[0];
  int filter_height = filter.size()[1];
  int filter_width = filter.size()[0];
  CudaPerformConvBackwardData(top_diff.data(), filter.data(), bottom_diff.data(), num_images, bottom_num_channels, top_num_channels, top_height, top_width, closure.pad_height, closure.pad_width, closure.stride_vertical, closure.stride_horizontal, filter_height, filter_width, context.stream, context.cudnn_handle);
}

void ConvBackwardFilter(const DataList& inputs, const DataList& outputs, ConvBackwardFilterClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 2) << "(conv backward filter) #inputs wrong";
  CHECK_EQ(outputs.size(), 1) << "(conv backward filter) #outputs wrong";
  auto& top_diff = inputs[0];
  auto& bottom = inputs[1];
  auto& filter_diff = outputs[0];
  int num_images = top_diff.size()[3];
  int bottom_num_channels = bottom.size()[2];
  int top_num_channels = top_diff.size()[2];
  int bottom_height = bottom.size()[1];
  int bottom_width = bottom.size()[0];
  int filter_height = filter_diff.size()[1];
  int filter_width = filter_diff.size()[0];
  CudaPerformConvBackwardFilter(bottom.data(), top_diff.data(), filter_diff.data(), num_images, bottom_num_channels, top_num_channels, bottom_height, bottom_width, closure.pad_height, closure.pad_width, closure.stride_vertical, closure.stride_horizontal, filter_height, filter_width, context.stream, context.cudnn_handle);
}

void ConvBackwardBias(const DataList& inputs, const DataList& outputs, ConvBackwardBiasClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 1) << "(conv backward bias) #inputs wrong";
  CHECK_EQ(outputs.size(), 1) << "(conv backward bias) #outputs wrong";
  auto& top_diff = inputs[0];
  auto& bias_diff = outputs[0];
  int num_images = top_diff.size()[3];
  int top_num_channels = top_diff.size()[2];
  int top_height = top_diff.size()[1];
  int top_width = top_diff.size()[0];
  CudaPerformConvBackwardBias(top_diff.data(), bias_diff.data(), num_images, top_num_channels, top_height, top_width, context.stream, context.cudnn_handle);
}

void SoftmaxForward(const DataList& inputs, const DataList& outputs, SoftmaxForwardClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 1) << "(softmax forward) #inputs wrong";
  CHECK_EQ(outputs.size(), 1) << "(softmax forward) #outputs wrong";
  auto& bottom = inputs[0];
  auto& top = outputs[0];
  int num_images = bottom.size()[3];
  int num_channels = bottom.size()[2];
  int height = bottom.size()[1];
  int width = bottom.size()[0];
  switch (closure.algorithm) {
    case SoftmaxAlgorithm::kInstance:
      CudaPerformInstanceSoftmaxForward(bottom.data(), top.data(), num_images, num_channels, height, width, context.stream, context.cudnn_handle);
    case SoftmaxAlgorithm::kChannel:
      CudaPerformChannelSoftmaxForward(bottom.data(), top.data(), num_images, num_channels, height, width, context.stream, context.cudnn_handle);
    default:
      CHECK(false) << "softmax algorithm not supported";
  }
}

void SoftmaxBackward(const DataList& inputs, const DataList& outputs, SoftmaxBackwardClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 2) << "(softmax backward) #inputs wrong";
  CHECK_EQ(outputs.size(), 1) << "(softmax backward) #outputs wrong";
  auto& top_diff = inputs[0];
  auto& top = inputs[1];
  auto& bottom_diff = outputs[0];
  int num_images = top_diff.size()[3];
  int num_channels = top_diff.size()[2];
  int height = top_diff.size()[1];
  int width = top_diff.size()[0];
  switch (closure.algorithm) {
    case SoftmaxAlgorithm::kInstance:
      CudaPerformInstanceSoftmaxBackward(top_diff.data(), top.data(), bottom_diff.data(), num_images, num_channels, height, width, context.stream, context.cudnn_handle);
    case SoftmaxAlgorithm::kChannel:
      CudaPerformChannelSoftmaxBackward(top_diff.data(), top.data(), bottom_diff.data(), num_images, num_channels, height, width, context.stream, context.cudnn_handle);
    default:
      CHECK(false) << "softmax algorithm not supported";
  }
}

void ActivationForward(const DataList& inputs, const DataList& outputs, ActivationForwardClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 1) << "(activation forward) #inputs wrong";
  CHECK_EQ(outputs.size(), 1) << "(activation forward) #outputs wrong";
  auto& bottom = inputs[0];
  auto& top = outputs[0];
  int num_images = bottom.size()[3];
  int num_channels = bottom.size()[2];
  int height = bottom.size()[1];
  int width = bottom.size()[0];
  switch (closure.algorithm) {
    case ActivationAlgorithm::kSigmoid:
      CudaPerformSigmoidForward(bottom.data(), top.data(), num_images, num_channels, height, width, context.stream, context.cudnn_handle);
    case ActivationAlgorithm::kRelu:
      CudaPerformReluForward(bottom.data(), top.data(), num_images, num_channels, height, width, context.stream, context.cudnn_handle);
    case ActivationAlgorithm::kTanh:
      CudaPerformTanhForward(bottom.data(), top.data(), num_images, num_channels, height, width, context.stream, context.cudnn_handle);
    default:
      CHECK(false) << "activation algorithm not supported";
  }
}

void ActivationBackward(const DataList& inputs, const DataList& outputs, ActivationBackwardClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 3) << "(activation backward) #inputs wrong";
  CHECK_EQ(outputs.size(), 1) << "(activation backward) #outputs wrong";
  auto& top_diff = inputs[0];
  auto& top = inputs[1];
  auto& bottom = inputs[2];
  auto& bottom_diff = outputs[0];
  int num_images = top_diff.size()[3];
  int num_channels = top_diff.size()[2];
  int height = top_diff.size()[1];
  int width = top_diff.size()[0];
  switch (closure.algorithm) {
    case ActivationAlgorithm::kSigmoid:
      CudaPerformSigmoidBackward(bottom.data(), top.data(), top_diff.data(), bottom_diff.data(), num_images, num_channels, height, width, context.stream, context.cudnn_handle);
    case ActivationAlgorithm::kRelu:
      CudaPerformReluBackward(bottom.data(), top.data(), top_diff.data(), bottom_diff.data(), num_images, num_channels, height, width, context.stream, context.cudnn_handle);
    case ActivationAlgorithm::kTanh:
      CudaPerformTanhBackward(bottom.data(), top.data(), top_diff.data(), bottom_diff.data(), num_images, num_channels, height, width, context.stream, context.cudnn_handle);
    default:
      CHECK(false) << "activation algorithm not supported";
  }
}

void PoolingForward(const DataList& inputs, const DataList& outputs, PoolingForwardClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 1) << "(pooling forward) #inputs wrong";
  CHECK_EQ(outputs.size(), 1) << "(pooling forward) #inputs wrong";
  auto& bottom = inputs[0];
  auto& top = outputs[0];
  int num_images = bottom.size()[3];
  int num_channels = bottom.size()[2];
  int bottom_height = bottom.size()[1];
  int bottom_width = bottom.size()[0];
  switch (closure.algorithm) {
    case PoolingInfo::Algorithm::kMax:
      CudaPerformMaxPoolingForward(bottom.data(), top.data(), num_images, num_channels, bottom_height, bottom_width, closure.stride_vertical, closure.stride_horizontal, closure.height, closure.width, context.stream, context.cudnn_handle);
    case PoolingInfo::Algorithm::kAverage:
      CudaPerformAveragePoolingForward(bottom.data(), top.data(), num_images, num_channels, bottom_height, bottom_width, closure.stride_vertical, closure.stride_horizontal, closure.height, closure.width, context.stream, context.cudnn_handle);
    default:
      CHECK(false) << "pooling algorithm not supported";
  }
}

void PoolingBackward(const DataList& inputs, const DataList& outputs, PoolingBackwardClosure& closure, const CudaRuntimeContext& context) {
  CHECK_EQ(inputs.size(), 3) << "(pooling backward) #inputs wrong";
  CHECK_EQ(outputs.size(), 1) << "(pooling backward) #inputs wrong";
  auto& top_diff = inputs[0];
  auto& top = inputs[1];
  auto& bottom = inputs[1];
  auto& bottom_diff = outputs[0];
  int num_images = top_diff.size()[3];
  int num_channels = top_diff.size()[2];
  int bottom_height = bottom.size()[1];
  int bottom_width = bottom.size()[0];
  switch (closure.algorithm) {
    case PoolingInfo::Algorithm::kMax:
      CudaPerformMaxPoolingBackward(bottom.data(), top.data(), top_diff.data(), bottom_diff.data(), num_images, num_channels, bottom_height, bottom_width, closure.stride_vertical, closure.stride_horizontal, closure.height, closure.width, context.stream, context.cudnn_handle);
    case PoolingInfo::Algorithm::kAverage:
      CudaPerformAveragePoolingBackward(bottom.data(), top.data(), top_diff.data(), bottom_diff.data(), num_images, num_channels, bottom_height, bottom_width, closure.stride_vertical, closure.stride_horizontal, closure.height, closure.width, context.stream, context.cudnn_handle);
    default:
      CHECK(false) << "pooling algorithm not supported";
  }
}

}
#endif
}

