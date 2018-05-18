#pragma once
#include <ops/ops.h>
#include <loops/reduce.h>
#include <loops/scalar.h>
#include <loops/indexreduce.h>
#include <loops/broadcasting.h>

namespace functions {
	namespace broadcast {
		template <typename T>
		class Broadcast;
	}

	namespace transform {
		template <typename T>
		class Transform;
	}

	namespace scalar {
	}

	namespace reduce {
		template <typename T>
		class ReduceFunction;
	}
}

namespace simdOps {

	template<typename T>
	class Pooling2D {
	public:
		static const bool requiresSpecial = true;
#ifdef __CUDACC__
		inline __host__ __device__
#elif defined(__GNUC__)

#endif
		static int outSize(int size, int k, int s, int p, bool coverAll) {
			if (coverAll)
				return (size + p * 2 - k + s - 1) / s + 1;
			else
				return (size + p * 2 - k) / s + 1;
		}

#ifdef __CUDACC__
		/**
		* Based on:  https://github.com/pjreddie/darknet/blob/master/src/im2col_kernels.cu
		*/

		static inline __device__ void execSpecialCuda(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, int *allocationPointer, T *reductionPointer, UnifiedSharedMemory *manager, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {

			__shared__ int kH;
			__shared__ int kW;
			__shared__ int sH;
			__shared__ int sW;
			__shared__ int pH;
			__shared__ int pW;
			__shared__ int dH;
			__shared__ int dW;
			__shared__ int poolingMode;
			__shared__ T extraParam0;

			__shared__ int batchSize;
			__shared__ int inChannels;
			__shared__ int outH;
			__shared__ int outW;
			__shared__ int inH;
			__shared__ int inW;

            //__shared__ int *strideIn;
            //__shared__ int *strideOut;
            __shared__ int strideB;
            __shared__ int strideC;
            __shared__ int strideY;
            __shared__ int strideX;

			__shared__ int strideOB;
            __shared__ int strideOC;
            __shared__ int strideOY;
            __shared__ int strideOX;

            __shared__ int length;
            __shared__ int kHEff;
            __shared__ int kWEff;
			__shared__ bool fOrder;
		

			if (threadIdx.x == 0) {
				kH = (int)extraParams[0];
				kW = (int)extraParams[1];
				sH = (int)extraParams[2];
				sW = (int)extraParams[3];
				pH = (int)extraParams[4];
				pW = (int)extraParams[5];
				dH = (int)extraParams[6];			//Dilation, height dimension
				dW = (int)extraParams[7];			//Dilation, width dimension
				poolingMode = (int)extraParams[9];
				extraParam0 = extraParams[10];

				batchSize = shape::sizeAt(xShapeBuffer, 0);
				inChannels = shape::sizeAt(xShapeBuffer, 1);
				outH = shape::sizeAt(resultShapeBuffer, 2);
				outW = shape::sizeAt(resultShapeBuffer, 3);
				inH = shape::sizeAt(xShapeBuffer, 2);
				inW = shape::sizeAt(xShapeBuffer, 3);

            	strideB = shape::stride(xShapeBuffer)[0];
            	strideC = shape::stride(xShapeBuffer)[1];
            	strideY = shape::stride(xShapeBuffer)[2];
            	strideX = shape::stride(xShapeBuffer)[3];

				strideOB = shape::stride(resultShapeBuffer)[0];
            	strideOC = shape::stride(resultShapeBuffer)[1];
            	strideOY = shape::stride(resultShapeBuffer)[2];
            	strideOX = shape::stride(resultShapeBuffer)[3];

            	length = shape::length(resultShapeBuffer);

				//Replace kernel H/W with *effective* kernel H/W accounting for dilatyon
				kHEff = kH + (kH-1)*(dH-1);
				kWEff = kW + (kW-1)*(dW-1);

				fOrder = shape::order(resultShapeBuffer) == 'f';
/*
				if (blockIdx.x == 0) {
					printf("kH: %i; kW: %i; sH: %i; sW: %i; pH: %i; pW: %i; dH: %i; dW: %i; poolingMode: %i; extraParam0: %f;\n", kH, kW, sH, sW, pH, pW, dH, dW, poolingMode, (float) extraParam0);
					printf("batchSize: %i; inChannels: %i; outH: %i; outW: %i; inH: %i; inW: %i; strideB: %i; strideC: %i; strideY: %i; strideX: %i;\n", batchSize, inChannels, outH, outW, inH, inW, strideB, strideC, strideY, strideX);
				}
*/
            }
            __syncthreads();

			int tid = blockIdx.x * gridDim.x + threadIdx.x;

            for (int index = tid; index < length; index += blockDim.x * gridDim.x) {
				const int pw = index % outW;
    			const int ph = (index / outW) % outH;
    			const int c = (index / outW / outH) % inChannels;
    			const int n = index / outW / outH / inChannels;
    			int hstart = sH * ph - pH;
    			int wstart = sW * pw - pW;
    			int hend = hstart + kHEff;
    			int wend = wstart + kWEff;

//    			const int hSO = hstart;
//    			const int hEO = hend;

    			if(hstart < 0){
                    int f = (int)nd4j::math::nd4j_ceil<T>((T) -hstart / (T)dH);
                    hstart += f * dH;
                }
                if(wstart < 0){
                    int f = (int)nd4j::math::nd4j_ceil<T>((T) -wstart / (T) dW);
                    wstart += f * dW;
                }
                if(hend > inH){
                    int f = (int)nd4j::math::nd4j_ceil<T>((T) (hend-inH) / (T) dH);
                    hend -= f * dH;
                }
                if(wend > inW){
                    int f = (int)nd4j::math::nd4j_ceil<T>((T) (wend-inW) / (T) dW);
                    wend -= f * dW;
                }
    			int pool_size = (int)(nd4j::math::nd4j_ceil<T>((T) (hend-hstart) / (T) dH) * (int) nd4j::math::nd4j_ceil<T>((T) (wend-wstart) / (T) dW));	//Accounts for dilation

    			T sum = poolingMode == 0 ? (T) -MAX_FLOAT : (T) 0;

    			T *input_slice = dx + (n * strideB + c * strideC);
    			if (poolingMode == 0) {
    			    for (int h = hstart; h < hend; h += dH) {
      				    for (int w = wstart; w < wend; w += dW) {
        				    T v = input_slice[h * strideY + w * strideX];
        				    if (v > sum)
        				        sum = v;
      				    }
    			    }
    			} else if (poolingMode == 1) {
    			    for (int h = hstart; h < hend; h += dH) {
      				    for (int w = wstart; w < wend; w += dW) {
        				    sum += input_slice[h * strideY + w * strideX];
      				    }
    			    }
    			} else if (poolingMode == 2) {
    			    for (int h = hstart; h < hend; h += dH) {
      				    for (int w = wstart; w < wend; w += dW) {
        				    sum += nd4j::math::nd4j_pow<T>(nd4j::math::nd4j_abs<T>(input_slice[h * strideY + w * strideX]), extraParam0);
      				    }
    			    }
    			}

				T res;

    			if (poolingMode == 0) {
                    res = sum;
    			} else if (poolingMode == 1) {
    			    int divide_factor = pool_size;  //Case 0: exclude padding
    			    if ((int) extraParam0 == 1)     //Case 1: include padding
					    divide_factor = kH * kW;

    			    res = sum / divide_factor;
    			} else if (poolingMode == 2) {
                    res = nd4j::math::nd4j_pow<T>(sum, (T) 1.0f / extraParam0);
    			}


				if (!fOrder) {
					result[index] = res;
                } else {
					result[n * strideOB + c * strideOC + pw * strideOX + ph * strideOY] = res;
                }
/*
                if (index >= 0 && index < 400000) {
    			    printf("index: %i; hstart: %i; hend: %i; wstart: %i; wend: %i; ph: %i; pw: %i; hstart_orig: %i; hend_orig: %i;\n", index, hstart, hend, wstart, wend, ph, pw, hSO, hEO);
    			}
*/
            }
		}
#endif


		static void execSpecial(
				T *dx,
				Nd4jLong *xShapeBuffer,
				T *result,
				Nd4jLong *resultShapeBuffer,
				T *extraParams, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {


			int kH = (int)extraParams[0];
			int kW = (int)extraParams[1];
			int sH = (int)extraParams[2];
			int sW = (int)extraParams[3];
			int pH = (int)extraParams[4];
			int pW = (int)extraParams[5];
			int dH = (int)extraParams[6];			//Dilation, height dimension
			int dW = (int)extraParams[7];			//Dilation, width dimension
			int poolingMode = (int)extraParams[9];
			T extraParam0 = extraParams[10];

            const int kHEff = kH + (kH-1)*(dH-1);
            const int kWEff = kW + (kW-1)*(dW-1);

			const int batchSize = (int) shape::sizeAt(xShapeBuffer, 0);
            const int inChannels = (int) shape::sizeAt(xShapeBuffer, 1);
            const int outH = (int) shape::sizeAt(resultShapeBuffer, 2);
            const int outW = (int) shape::sizeAt(resultShapeBuffer, 3);
            const int inH = (int) shape::sizeAt(xShapeBuffer, 2);
            const int inW = (int) shape::sizeAt(xShapeBuffer, 3);

            auto strideIn = shape::stride(xShapeBuffer);
            auto strideOut = shape::stride(resultShapeBuffer);

            const bool fOrder = shape::order(resultShapeBuffer) == 'f';
            const Nd4jLong zLength = shape::length(resultShapeBuffer);
            const int zRank = shape::rank(resultShapeBuffer);

            int indices[6];

            int idx = 0;
#pragma omp parallel for collapse(2) schedule(guided) shared(indices)
			for(int k = 0; k < inChannels; k++)
			{
				for(int p = 0; p < batchSize; p++)
				{
					int xx, yy;
					/* For all output pixels... */
					const int _b = p * strideOut[0];
					const int _k = k * strideOut[1];
					T *ptr_output = result + _b + _k;
					T *ptr_input = dx + p * strideIn[0] + k * strideIn[1];

					for(yy = 0; yy < outH; yy++)
					{
						for(xx = 0; xx < outW; xx++)
						{
							/* Compute the mean of the input image... */
							int hstart = yy * sH - pH;
							int wstart = xx * sW - pW;
                            int hend = hstart + kHEff;
                            int wend = wstart + kWEff;
                            const int hSO = hstart;
                            const int hEO = hend;
							if(hstart < 0){
								int n = (int)nd4j::math::nd4j_ceil<T>((T) -hstart / ((T)dH));
								hstart += n * dH;
							}
							if(wstart < 0){
								int n = (int)nd4j::math::nd4j_ceil<T>((T) -wstart / ((T)dW));
								wstart += n * dW;
							}
                            if(hend > inH){
                                int n = (int)nd4j::math::nd4j_ceil<T>((T)(hend-inH)/((T)dH));
                                hend -= n * dH;
                            }
                            if(wend > inW){
                                int n = (int)nd4j::math::nd4j_ceil<T>((T)(wend-inW)/((T)dW));
                                wend -= n * dW;
                            }
                            int pool_size = (int)(nd4j::math::nd4j_ceil<T>((T) (hend-hstart)/((T)dH))
                                                  * (int)nd4j::math::nd4j_ceil<T>((T)(wend-wstart)/((T)dW)));	//Accounts for dilation

							T sum = poolingMode == 0 ? (T) -MAX_FLOAT : (T) 0;

							// we need this only for avg pooling
							int divide_factor = 0;
							if (poolingMode == 1) {
								if ((int) extraParam0 == 0)         //Exclude padding
									divide_factor = pool_size;
								else if ((int) extraParam0 == 1)    //Include padding
                                    divide_factor = kH * kW;
							}

							long kx, ky;

							if (poolingMode == 0) {
#pragma omp simd reduction(maxT:sum) collapse(2)
								for (ky = hstart; ky < hend; ky += dH) {
									for (kx = wstart; kx < wend; kx += dW)
										if (ptr_input[ky * strideIn[2] + kx * strideIn[3]] > sum)
											sum = ptr_input[ky * strideIn[2] + kx * strideIn[3]];
								}
							} else if (poolingMode == 1) {
#pragma omp simd reduction(sumT:sum) collapse(2)
								for (ky = hstart; ky < hend; ky += dH) {
									for (kx = wstart; kx < wend; kx += dW)
										sum += ptr_input[ky * strideIn[2] + kx * strideIn[3]];
								}
							} else if (poolingMode == 2) {
#pragma omp simd reduction(sumT:sum) collapse (2)
								for (ky = hstart; ky < hend; ky += dH) {
									for (kx = wstart; kx < wend; kx += dW)
										sum += nd4j::math::nd4j_pow<T>(nd4j::math::nd4j_abs<T>(ptr_input[ky * strideIn[2] + kx * strideIn[3]]), extraParam0);
								}
							}
							/* Update output */
							T res = sum;

							if (poolingMode == 1) {
                                res /= divide_factor;
                            } else if (poolingMode == 2)
								res = nd4j::math::nd4j_pow<T>(res, (T) 1.0f / extraParam0);


                            if (!fOrder) {
                                *ptr_output++ = res;
                            } else {
								result[_b + _k + yy * strideOut[2] + xx * strideOut[3]] = res;
                            }
						}
					}
				}
			}
		}

		op_def static T op(T d1, T *params) {
			return d1;
		}


		/** Calculate buffer offset (like Shape.getOffset) without checking on input for negative indices etc
		*  normally negative indices are bad, OK here because of other checks on input indices
		*  Uses unrolled loop specifically for length 4
		*/
#ifdef __CUDACC__
		inline __host__ __device__
#elif defined(__GNUC__)


#endif
		static int getOffsetUnsafe4(int baseOffset, int *shape, int *stride, int *indices) {
			int offset = baseOffset;
			if (shape[0] != 1) offset += indices[0] * stride[0];
			if (shape[1] != 1) offset += indices[1] * stride[1];
			if (shape[2] != 1) offset += indices[2] * stride[2];
			if (shape[3] != 1) offset += indices[3] * stride[3];
			return offset;
		}


		/**
		* A version of Shape.getOffset without checking on input for negative indices etc
		* normally negative indices are bad, OK here because of other checks on input indices
		* Uses unrolled loop specifically for length 6, where indices[2] and indices[3] are zero (always are here)
		*/
#ifdef __CUDACC__
		inline __host__ __device__
#elif defined(__GNUC__)


#endif
		static int getOffsetUnsafe6(int baseOffset, int *shape, int *stride, int *indices) {
			int offset = baseOffset;
			if (shape[0] != 1) offset += indices[0] * stride[0];
			if (shape[1] != 1) offset += indices[1] * stride[1];
			if (shape[4] != 1) offset += indices[4] * stride[4];
			if (shape[5] != 1) offset += indices[5] * stride[5];
			return offset;
		}

	};


    FORCEINLINE bool is_a_ge_zero_and_a_lt_b(int a, int b) {
        return static_cast<unsigned>(a) < static_cast<unsigned>(b);
    }

	template<typename T>
	class 
	Im2col {
	public:
		static const bool requiresSpecial = true;
#ifdef __CUDACC__
		inline __host__ __device__
#elif defined(__GNUC__)

#endif
		static int outSize(int size, int k, int s, int p, bool coverAll) {
			if (coverAll)
				return (size + p * 2 - k + s - 1) / s + 1;
			else
				return (size + p * 2 - k) / s + 1;
		}

#ifdef __CUDACC__
		/**
		* Based on:  https://github.com/pjreddie/darknet/blob/master/src/im2col_kernels.cu
		*/

		static inline __device__ void execSpecialCuda(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, int *allocationPointer, T *reductionPointer, UnifiedSharedMemory *manager, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {
			/*kernel[0], kernel[1], stride[0], stride[1], padding[0], padding[1], 0, false*/
			int kernelHeight = (int)extraParams[0];
			int kernelWidth = (int)extraParams[1];
			int strideY = (int)extraParams[2];
			int strideX = (int)extraParams[3];
			int padHeight = (int)extraParams[4];
			int padWidth = (int)extraParams[5];
			int dY = (int)extraParams[6];			//Dilation, height/y dimension
			int dX = (int)extraParams[7];			//Dilation, width/x dimension
			int kSize = kernelWidth * kernelHeight;
			T zeroPadVal = (T)extraParams[9];	//Value to use when value is padding. Usually 0 but not always

			auto outShape = shape::shapeOf(resultShapeBuffer);
			auto resultOrder = shape::order(resultShapeBuffer);
			auto outStride = shape::stride(resultShapeBuffer);

			auto inShape = shape::shapeOf(xShapeBuffer);
			auto inStride = shape::stride(xShapeBuffer);

			int samples = inShape[0];
			int depth = inShape[1];
			int height = inShape[2];
			int width = inShape[3];


			int strideex = inStride[0];
			int stridech = inStride[1];
			int strideh = inStride[2];
			int stridew = inStride[3];

			// (height + 2 * padHeight - kernelHeight) / strideX + 1; //
			// (width + 2 * padWidth - kernelWidth) / strideY + 1; //
			int height_col = outShape[4];
			int width_col = outShape[5];

			int n = samples * depth * height_col * width_col;
			/*
			if (threadIdx.x == 0)
			printf("Kernel h: [%i], w: [%i]; Col h: [%i], w: [%i]; Stride x: [%i], y: [%i]; Height: [%i], Width: [%i], Depth: [%i], N: [%i], Samples: [%i]\n",
			kernelHeight, kernelWidth, height_col, width_col, strideX, strideY, height, width, depth, n, samples);
			*/

			int index = blockIdx.x * blockDim.x + threadIdx.x;
			for (; index < n; index += blockDim.x*gridDim.x) {
				int h_index = index / width_col;
				int h_col = h_index % height_col;
				int w_col = index % width_col;

				int c_im = h_index / height_col;
				int c_col = c_im * kSize;

				int depth_im = c_im % depth;
				int num_im = c_im / depth;
				int h_offset = h_col * strideY - padHeight;
				int w_offset = w_col * strideX - padWidth;

				T* data_col_ptr = result;

				int i_c = (c_col * height_col + h_col) * width_col + w_col;
				data_col_ptr += (c_col * height_col + h_col) * width_col + w_col;

				T* data_im_ptr = dx;

				data_im_ptr += num_im * strideex + depth_im * stridech + h_offset * strideh + w_offset*stridew;

				for (int i = 0; i < kernelHeight; ++i) {
					for (int j = 0; j < kernelWidth; ++j) {
						int h_im = h_offset + i * dY;
						int w_im = w_offset + j * dX;
						int i_f = 0;
						int i_c_temp = i_c;
						for (int dim = 5; dim >= 0; dim--) {
							i_f += (i_c_temp % outShape[dim])  * outStride[dim];
							i_c_temp = i_c_temp / outShape[dim];
						}
						if (h_im >= 0 && w_im >= 0 && h_im < height && w_im < width){
							result[i_f] = data_im_ptr[i * dY * strideh + j * dX * stridew];
						} else result[i_f] = zeroPadVal;

						//result[i_f] = (h_im >= 0 && w_im >= 0 && h_im < height && w_im < width) ? data_im_ptr[i * strideh + j*stridew] : 0;
						data_col_ptr += height_col * width_col;
						i_c += height_col * width_col;
					}
				}
			}
		}
#endif


		static void execSpecial(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {
			/*kernel[0], kernel[1], stride[0], stride[1], padding[0], padding[1], 0, false*/

			T zeroPadVal = (T) 0.0f;

			int kH = (int)extraParams[0];
			int kW = (int)extraParams[1];
			int sH = (int)extraParams[2];
			int sW = (int)extraParams[3];
			int pH = (int)extraParams[4];
			int pW = (int)extraParams[5];
			int dH = (int)extraParams[6];			//Dilation, height/y dimension
			int dW = (int)extraParams[7];			//Dilation, width/x dimension

            auto outShape  = shape::shapeOf(resultShapeBuffer);
            auto outStride = shape::stride(resultShapeBuffer);
            auto inShape = shape::shapeOf(xShapeBuffer);
            auto inStride = shape::stride(xShapeBuffer);

                const int bS = inShape[0];
                const int iC = inShape[1];
                const int iH = inShape[2];
                const int iW = inShape[3];
                const int oH = outShape[4];
                const int oW = outShape[5];
                const int outStride0  = outStride[0];
                const int outStride1  = outStride[1];
                const int outStride2  = outStride[2];
                const int outStride3  = outStride[3];
                const int outStride4  = outStride[4];
                const int outStride5  = outStride[5];
                const int inStride0   = inStride[0];
                const int inStride1   = inStride[1];
                const int inStride2   = inStride[2];
                const int inStride3   = inStride[3];

                const T* in0End = dx + inStride1 * iC;
                const int kRowEnd = -pH + kH * dH;
                const int kColEnd = -pW + kW * dW;
                const int oHW = oH * oW;
                const int inRowEnd = oH * sH;
                const int inColEnd = oW * sW;

				int inRowStart, inColStart, inRow, inCol;
                T *in0, *in1;

                if (shape::order(xShapeBuffer) == 'c' &&  shape::order(resultShapeBuffer) == 'c' && shape::strideDescendingCAscendingF(xShapeBuffer) && shape::strideDescendingCAscendingF(resultShapeBuffer)) {

#pragma omp parallel for schedule(static) proc_bind(close) private(in0, in1, inRowStart, inColStart, inRow, inCol)
					for (int b = 0; b < bS; b++) {
						in0 = dx + (b * inStride0);
						T *output = result + (b * outStride0);

						for (int channel = 0; channel < iC; ++channel, in0 += inStride1) {

							for (int kRow = 0; kRow < kH; kRow++) {
								inRowStart = -pH + kRow * dH;

								for (int kCol = 0; kCol < kW; kCol++) {
									inRow = inRowStart;
									inColStart = -pW + kCol * dW;

									for (int outRow = 0; outRow < oH; ++outRow, inRow += sH) {

										if (!is_a_ge_zero_and_a_lt_b(inRow, iH))
											for (int outCol = 0; outCol < oW; ++outCol, ++output) {
												*output = zeroPadVal;
											}
										else {
											inCol = inColStart;
											in1 = in0 + inRow * inStride2;

											for (int outCol = 0; outCol < oW; ++outCol, inCol += sW, ++output)
												if (is_a_ge_zero_and_a_lt_b(inCol, iW))
													*output = *(in1 + inCol * inStride3);
												else
													*output = zeroPadVal;
										}
									}
								}
							}
						}
					}
                } 
                else {
					T *out0, *out1, *out2, *out3, *out4;
#pragma omp parallel for schedule(static) proc_bind(close) private(in0, in1, out0, out1, out2, out3, out4, inRowStart, inColStart, inRow, inCol)
					for (int b = 0; b < bS; b++) {
						in0 = dx + (b * inStride0);
						out0  = result + b * outStride0;

						for (int channel = 0; channel < iC; ++channel, in0 += inStride1, out0+=outStride1) {
							out1 = out0;

							for (int kRow = 0; kRow < kH; kRow++, out1 += outStride2) {
								out2 = out1;
								inRowStart = -pH + kRow * dH;

								for (int kCol = 0; kCol < kW; kCol++, out2 += outStride3) {
									out3 = out2;
									inRow = inRowStart;
									inColStart = -pW + kCol * dW;

									for (int outRow = 0; outRow < oH; ++outRow, inRow += sH, out3 += outStride4) {
										out4 = out3;

										if (!is_a_ge_zero_and_a_lt_b(inRow, iH))
											for (int outCol = 0; outCol < oW; ++outCol, out4 += outStride5) {
												*out4 = zeroPadVal;
											}
										else {
											inCol = inColStart;
											in1 = in0 +  inRow * inStride2;

											for (int outCol = 0; outCol < oW; ++outCol, inCol += sW, out4 += outStride5) {
												if (is_a_ge_zero_and_a_lt_b(inCol, iW))
													*out4 = *(in1 + inCol * inStride3);
												else
													*out4 = zeroPadVal;
											}
										}
									}
								}
							}
						}
					}
                }

		}

		op_def static T op(T d1, T *params) {
			return d1;
		}


		/** Calculate buffer offset (like Shape.getOffset) without checking on input for negative indices etc
		*  normally negative indices are bad, OK here because of other checks on input indices
		*  Uses unrolled loop specifically for length 4
		*/
#ifdef __CUDACC__
		inline __host__ __device__
#elif defined(__GNUC__)


#endif
		static int getOffsetUnsafe4(int baseOffset, int *shape, int *stride, int *indices) {
			int offset = baseOffset;
			if (shape[0] != 1) offset += indices[0] * stride[0];
			if (shape[1] != 1) offset += indices[1] * stride[1];
			if (shape[2] != 1) offset += indices[2] * stride[2];
			if (shape[3] != 1) offset += indices[3] * stride[3];
			return offset;
		}


		/**
		* A version of Shape.getOffset without checking on input for negative indices etc
		* normally negative indices are bad, OK here because of other checks on input indices
		* Uses unrolled loop specifically for length 6, where indices[2] and indices[3] are zero (always are here)
		*/
#ifdef __CUDACC__
		inline __host__ __device__
#elif defined(__GNUC__)


#endif
		static int getOffsetUnsafe6(int baseOffset, int *shape, int *stride, int *indices) {
			int offset = baseOffset;
			if (shape[0] != 1) offset += indices[0] * stride[0];
			if (shape[1] != 1) offset += indices[1] * stride[1];
			if (shape[4] != 1) offset += indices[4] * stride[4];
			if (shape[5] != 1) offset += indices[5] * stride[5];
			return offset;
		}

	};

	template<typename T>
	class Histogram {
	public:
		static const bool requiresSpecial = true;

#ifdef __CUDACC__
		static inline __device__ void execSpecialCuda(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, int *allocationPointer, T *reductionPointer, UnifiedSharedMemory *manager, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {

            int numBins = (int) extraParams[0];
            T min_val = extraParams[1];
            T max_val = extraParams[2];

            int tid = blockIdx.x * blockDim.x + threadIdx.x;

            __shared__ T *bins;
            __shared__ int length;
            __shared__ T *reductor;
            if (threadIdx.x == 0) {
                extern __shared__ unsigned char shmem[];
                bins = (T *) shmem;
                reductor = ((T *) allocationPointer) + (numBins * blockIdx.x);

                length = shape::length(xShapeBuffer);
            }
            __syncthreads();

            T binSize = (max_val - min_val) / (numBins);

            for (int e = threadIdx.x; e < numBins; e += blockDim.x) {
                bins[e] = (T) 0.0f;
            }
            __syncthreads();

            for (int e = tid; e < length; e+= blockDim.x * gridDim.x) {
                int idx = (int) ((dx[e] - min_val) / binSize);
				    if (idx < 0) idx = 0;
					else if (idx >= numBins) idx = numBins - 1;

				nd4j::math::atomics::nd4j_atomicAdd(&bins[idx], (T) 1.0f);
            }
            __syncthreads();

            // transfer shared memory to reduction memory


            if (gridDim.x > 1) {
                unsigned int *tc = (unsigned int *)reductionPointer;
                __shared__ bool amLast;

                for (int e = threadIdx.x; e < numBins; e += blockDim.x) {
                    reductor[e] = bins[e];
                }
                __threadfence();
                __syncthreads();

                if (threadIdx.x == 0) {
						unsigned int ticket = atomicInc(&tc[16384], gridDim.x);
						amLast = (ticket == gridDim.x - 1);
				}
				__syncthreads();

				if (amLast) {
				    tc[16384] = 0;

                    // nullify shared memory for future accumulation
                    for (int e = threadIdx.x; e < numBins; e += blockDim.x) {
                        bins[e] = (T) 0.0f;
                    }

                    // accumulate reduced bins
                    for (int r = 0; r < gridDim.x; r++) {
                        T *ptrBuf = ((T *)allocationPointer) + (r * numBins);

                        for (int e = threadIdx.x; e < numBins; e += blockDim.x) {
                            bins[e] += ptrBuf[e];
                        }
                    }
                    __syncthreads();

                    // write them out to Z
                    for (int e = threadIdx.x; e < numBins; e += blockDim.x) {
                        result[e] = bins[e];
                    }
				}
            } else {
                // if there's only 1 block - just write away data
                for (int e = threadIdx.x; e < numBins; e += blockDim.x) {
                    result[e] = bins[e];
                }
            }

		};
#endif

		static void execSpecial(
				T *dx,
				Nd4jLong *xShapeBuffer,
				T *result,
				Nd4jLong *resultShapeBuffer,
				T *extraParams, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {

			int length = shape::length(xShapeBuffer);
			int _threads = 2;

			int numBins = (int) extraParams[0];
			int span = (length / _threads) + 8;

			// get min over input
            T min_val = extraParams[1];
            T max_val = extraParams[2];

            /*
#pragma omp parallel for simd num_threads(_threads) if (_threads > 1) reduction(min:min_val) proc_bind(close)
            for (int x = 0; x < length; x++) {
				if (min_val > dx[x])
					min_val = dx[x];
			}

			// get max over input
			T max_val = (T) MIN_FLOAT;

#pragma omp parallel for simd num_threads(_threads) if (_threads > 1) reduction(max:max_val) proc_bind(close)
			for (int x = 0; x < length; x++) {
				if (max_val < dx[x])
					max_val = dx[x];
			}
            */

			T binSize = (max_val - min_val) / (numBins);


#pragma omp parallel num_threads(_threads) if (_threads > 1) proc_bind(close) default(shared)
			{
				int tid, start, end;

				int *bins = new int[numBins];
                std::memset(bins, 0, sizeof(int) * numBins);
				tid = omp_get_thread_num();
				start = span * tid;
				end = span * (tid + 1);
				if (end > length) end = length;

#pragma omp simd
				for (int x = start; x < end; x++) {
					int idx = (int) ((dx[x] - min_val) / binSize);
					if (idx < 0)
						idx = 0;
					else if (idx >= numBins)
						idx = numBins - 1;

					bins[idx]++;
				}

#pragma omp critical
				{
#pragma omp simd
					for (int x = 0; x < numBins; x++) {
						result[x] += bins[x];
					}

				}

				delete[] bins;
			}

		}


        op_def static T op(T d1, T *params) {
            return d1;
        }
	};

	template<typename T>
	class Col2Im {

	public:
		static const bool requiresSpecial = true;
#ifdef __CUDACC__
		/**
		* https://github.com/pjreddie/darknet/blob/master/src/col2im_kernels.cu
		*/

		static inline __device__ void execSpecialCuda(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, int *allocationPointer, T *reductionPointer, UnifiedSharedMemory *manager, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {
			auto inShape = shape::shapeOf(xShapeBuffer);
			auto inStride = shape::stride(xShapeBuffer);

			int strideex = inStride[0];
			int stridech = inStride[1];
			int stridekrow = inStride[2];
			int stridekcol = inStride[3];
			int striderow = inStride[4];
			int stridecol = inStride[5];

			int kernelHeight = inShape[2];
			int kernelWidth = inShape[3];

			// C

			int strideY = (int)extraParams[0];
			int strideX = (int)extraParams[1];
            int padHeight = (int)extraParams[2];
			int padWidth = (int)extraParams[3];
            int imgHeight = (int)extraParams[4];
            int imgWidth = (int)extraParams[5];
			int dY = (int)extraParams[6];			//Dilation in height/y dimension
            int dX = (int)extraParams[7];			//Dilation in width/x dimension

			auto outShape = shape::shapeOf(resultShapeBuffer);
			auto resultOrder = shape::order(resultShapeBuffer);
			auto outStride = shape::stride(resultShapeBuffer);

			int samples = outShape[0];
			int depth = outShape[1];
			int imgH = outShape[2];
			int imgW = outShape[3];

			int height_col = inShape[4];//(imgHeight + 2 * padHeight - kernelHeight) / strideX + 1;
			int width_col = inShape[5];//(imgWidth + 2 * padWidth - kernelWidth) / strideY + 1;

			int n = samples * depth * imgHeight * imgWidth;

			/*if (threadIdx.x == 0)
			printf("Kernel h: [%i], w: [%i]; Col h: [%i], w: [%i]; Stride x: [%i], y: [%i]; Height: [%i], Width: [%i], Depth: [%i], N: [%i], Samples: [%i]\n",
			kernelHeight, kernelWidth, height_col, width_col, strideX, strideY, imgHeight, imgWidth, depth, n, samples);*/

			//Effective kernel size, accounting for dilation
			int kEffectiveW = kernelWidth + (kernelWidth - 1) * (dX - 1);
			int kEffectiveH = kernelHeight + (kernelHeight - 1) * (dY - 1);

			for (int i = (blockDim.x * blockIdx.x) + threadIdx.x; i < n; i += blockDim.x * gridDim.x) {
				T val = 0;
				int w_im = i % imgWidth + padWidth;
				int h_im = (i / imgWidth) % imgHeight + padHeight;
				int c_im = i / (imgWidth * imgHeight);

				int num_im = c_im / depth;
				int depth_im = c_im % depth;

				// compute the start and end of the output
				// These are the indexes for dimensions ??? in the 6d col matrix
				int w_col_start = (w_im < kEffectiveW) ? 0 : (w_im - kEffectiveW) / strideX + 1;
				int w_col_end = nd4j::math::nd4j_min<int>(w_im / strideX + 1, width_col);

				int h_col_start = (h_im < kEffectiveH) ? 0 : (h_im - kEffectiveH) / strideY + 1;
				int h_col_end = nd4j::math::nd4j_min<int>(h_im / strideY + 1, height_col);


				//Iterate over col entries in the 6d array... these are added up
				for (int h_col = h_col_start; h_col < h_col_end; h_col += 1) {
					for (int w_col = w_col_start; w_col < w_col_end; w_col += 1) {
						int h_k = (h_im - h_col * strideY);
						int w_k = (w_im - w_col * strideX);
						
						if(h_k % dY == 0 && w_k % dX == 0){
							h_k /= dY;
							w_k /= dX;

							int data_col_index = num_im * strideex + depth_im * stridech + h_k * stridekrow + w_k * stridekcol + h_col * striderow + w_col * stridecol;
							val += dx[data_col_index];
						}
					}
				}
				int i_f = 0;
				int i_c = i;
				for (int dim = 3; dim >= 0; dim--)
				{
					i_f += (i_c % outShape[dim])  * outStride[dim];
					i_c = i_c / outShape[dim];
				}
				result[i_f] = val;
			}
		}
#endif

		static void execSpecial(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {

            const Nd4jLong *inShape = shape::shapeOf(xShapeBuffer);
            const Nd4jLong *inStride = shape::stride(xShapeBuffer);
            const Nd4jLong *outShape = shape::shapeOf(xShapeBuffer);
            const Nd4jLong *outStride = shape::stride(resultShapeBuffer);
			
			const int kH = inShape[2];
			const int kW = inShape[3];        
			const int bS = outShape[0];
			const int iC = outShape[1];
			const int oH = inShape[4];                            
			const int oW = inShape[5];                            

			const int sH = (int)extraParams[0];
			const int sW = (int)extraParams[1];
            const int pH = (int)extraParams[2];
			const int pW = (int)extraParams[3];
            const int iH = (int)extraParams[4];
            const int iW = (int)extraParams[5];
			const int dH = (int)extraParams[6];		
            const int dW = (int)extraParams[7];		

			const int inStride0  = inStride[0];
            const int inStride1  = inStride[1];
            const int inStride2  = inStride[2];
            const int inStride3  = inStride[3];
            const int inStride4  = inStride[4];
            const int inStride5  = inStride[5];
            const int outStride0 = outStride[0];
            const int outStride1 = outStride[1];
            const int outStride2 = outStride[2];
            const int outStride3 = outStride[3];

            const T* out0End = result + outStride1 * iC;
            const int kRowEnd = -pH + kH * dH;
            const int inStepOW = oW * inStride5;
            const int kColEnd = -pW + kW * dW;
            const int inRowEnd = oH * sH;
            const int inColEnd = oW * sW;

            int inRowStart, inColStart, inRow, inCol;
            T *out0, *out1, *out2;

            memset(result, 0, shape::length(resultShapeBuffer) * sizeof(T));

                if (shape::order(xShapeBuffer) == 'c' &&  shape::order(resultShapeBuffer) == 'c' && shape::strideDescendingCAscendingF(xShapeBuffer) && shape::strideDescendingCAscendingF(resultShapeBuffer)) {

#pragma omp parallel for schedule(guided) proc_bind(close) private(out0, out1, out2, inRowStart, inColStart, inRow, inCol)
                    for (int b = 0; b < bS; b++) {
                        T *input = dx + (b * inStride0);
                        out0 = result + (b * outStride0);

                        for (int channel = 0; channel < iC; ++channel, out0 += outStride1) {

                            for (int kRow = 0; kRow < kH; ++kRow) {
                                inRowStart = -pH + kRow * dH;

                                for (int kCol = 0; kCol < kW; ++kCol) {
                                    inRow = inRowStart;
                                    inColStart = -pW + kCol * dW;

                                    for (int outRow = 0; outRow < oH; ++outRow, inRow += sH) {

                                        if (!is_a_ge_zero_and_a_lt_b(inRow, iH)) {
                                            input += inStepOW;
                                        }
                                        else {
                                            inCol = inColStart;
                                            out1 = out0 + inRow * outStride2;

                                            for (int outCol = 0; outCol < oW; ++outCol, inCol += sW, input += inStride5) {
                                                if (is_a_ge_zero_and_a_lt_b(inCol, iW)) {
                                                    out2 = out1 + inCol * outStride3;
                                                    *out2 += *input;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } 
                else {

                    T *in0, *in1, *in2, *in3, *in4;
#pragma omp parallel for schedule(guided) proc_bind(close) private(in0, in1, in2, in3, in4, out0, out1, out2, inRowStart, inColStart, inRow, inCol)
                    for (int b = 0; b < bS; b++) {
                        out0 = result + (b * outStride0);
                        in0 = dx + b * inStride0;

                        for (int channel = 0; channel < iC; ++channel, out0+=outStride1, in0+=inStride1) {
                            in1 = in0;

                            for (int kRow = 0; kRow < kH; ++kRow, in1+=inStride2) {
                                in2 = in1;
                                inRowStart = -pH + kRow * dH;

                                for (int kCol = 0; kCol < kW; ++kCol, in2+=inStride3) {
                                    in3 = in2;
                                    inRow = inRowStart;
                                    inColStart = -pW + kCol * dW;

                                    for (int outRow = 0; outRow < oH; ++outRow, inRow+=sH, in3+=inStride4) {
                                        in4 = in3;

                                        if (!is_a_ge_zero_and_a_lt_b(inRow, iH)) {
                                            in4 += inStepOW;
                                        }
                                        else {
                                            inCol = inColStart;
                                            out1 = out0 + inRow * outStride2;

                                            for (int outCol = 0; outCol < oW; ++outCol, inCol+=sW, in4+=inStride5) {
                                                if (is_a_ge_zero_and_a_lt_b(inCol, iW)) {
                                                    out2 = out1 + inCol * outStride3;
                                                    *out2 += *in4;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
		}

		op_def static T op(T d1, T *params) {
			return d1;
		}


		/** Calculate buffer offset (like Shape.getOffset) without checking on input for negative indices etc
		*  normally negative indices are bad, OK here because of other checks on input indices
		*  Uses unrolled loop specifically for length 4
		*/
#ifdef __CUDACC__
		inline __host__ __device__
#elif defined(__GNUC__)


#endif
		static int getOffsetUnsafe4(int baseOffset, int *shape, int *stride, int *indices) {
			int offset = baseOffset;
			if (shape[0] != 1) offset += indices[0] * stride[0];
			if (shape[1] != 1) offset += indices[1] * stride[1];
			if (shape[2] != 1) offset += indices[2] * stride[2];
			if (shape[3] != 1) offset += indices[3] * stride[3];
			return offset;
		}

		/** A version of Shape.getOffset without checking on input for negative indices etc
		* normally negative indices are bad, OK here because of other checks on input indices
		* Uses unrolled loop specifically for length 6, where indices[2] and indices[3] are zero (always are here)
		*/
#ifdef __CUDACC__
		inline __host__ __device__
#elif defined(__GNUC__)


#endif
		static int getOffsetUnsafe6(int baseOffset, int *shape, int *stride, int *indices) {
			int offset = baseOffset;
			if (shape[0] != 1) offset += indices[0] * stride[0];
			if (shape[1] != 1) offset += indices[1] * stride[1];
			if (shape[4] != 1) offset += indices[4] * stride[4];
			if (shape[5] != 1) offset += indices[5] * stride[5];
			return offset;
		}

	};


	template<typename T>
	class Reverse {
	public:
		static const bool requiresSpecial = true;

#ifdef __CUDACC__
		static inline __device__ void execSpecialCuda(T *dx, Nd4jLong *xShapeBuffer, T *result, Nd4jLong *zShapeBuffer, T *extraParams, int *allocationPointer, T *reductionPointer, UnifiedSharedMemory *manager, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {
            __shared__ Nd4jLong xLength;
			__shared__ int xEWS;
            __shared__ char xOrder;
            __shared__ Nd4jLong sLength;
            __shared__ T *shmem;
            int tid = threadIdx.x + blockIdx.x * blockDim.x;

            if (threadIdx.x == 0) {
                xLength = shape::length(xShapeBuffer);
			    xEWS = shape::elementWiseStride(xShapeBuffer);
                xOrder = shape::order(xShapeBuffer);
                sLength = xLength - 1;

                extern __shared__ unsigned char shrd[];
                shmem = (T *) shrd;
            }
            __syncthreads();



            if (dx == result) {

                if (xEWS == 1) {
                    for (int e = tid; e < xLength / 2; e += blockDim.x * gridDim.x) {
                        Nd4jLong idx = sLength - e;
                        T tmp = dx[e];
                        dx[e] = dx[idx];
                        dx[idx] = tmp;
                    }
                } else if (xEWS >= 1) {
                    for (int e = tid; e < xLength / 2; e += blockDim.x * gridDim.x) {
                        Nd4jLong idx1 = (sLength - e) * xEWS;
                        Nd4jLong idx2 =  e * xEWS;
                        T tmp = dx[idx2];
                        dx[idx2] = dx[idx1];
                        dx[idx1] = tmp;
                    }
                } else {
                    __shared__ int xRank;
                    __shared__ Nd4jLong *xShape;
                    __shared__ Nd4jLong *xStride;

                    if (threadIdx.x == 0) {
				        xRank = shape::rank(xShapeBuffer);
                        xShape = shape::shapeOf(xShapeBuffer);
                        xStride = shape::stride(xShapeBuffer);
				    }
				    __syncthreads();

					Nd4jLong xCoord[MAX_RANK];
					Nd4jLong zCoord[MAX_RANK];

					for (int e = tid; e < xLength / 2; e += blockDim.x * gridDim.x) {
                        if (xOrder == 'c') {
                            shape::ind2subC(xRank, xShape, e, xCoord);
                            shape::ind2subC(xRank, xShape, sLength - e, zCoord);
                        } else {
                            shape::ind2sub(xRank, xShape, e, xCoord);
                            shape::ind2sub(xRank, xShape, sLength - e, zCoord);
                        }

                        auto xOffset = shape::getOffset(0, xShape, xStride, xCoord, xRank);
                        auto zOffset = shape::getOffset(0, xShape, xStride, zCoord, xRank);

                        result[zOffset] = dx[xOffset];
					}
                }

            } else {
                __shared__ int zEWS;
				__shared__ char zOrder;

				if (threadIdx.x == 0) {
				    zEWS = shape::elementWiseStride(zShapeBuffer);
				    zOrder = shape::order(zShapeBuffer);
				}
				__syncthreads();

                if (xEWS == 1 && zEWS == 1 && xOrder == zOrder) {
                    // loop for whole array
                    for (int e = tid; e < xLength; e += blockDim.x * gridDim.x) {
                        result[sLength - e] = dx[e];
                    }
                } else if (xEWS >= 1 && zEWS >= 1 && xOrder == zOrder) {

                    for (int e = tid; e < xLength; e += blockDim.x * gridDim.x) {
                        result[(sLength - e) * zEWS] = dx[e * xEWS];
                    }
                } else {
                    __shared__ int xRank;
                    __shared__ Nd4jLong *xShape;
                    __shared__ Nd4jLong *xStride;

					__shared__ int zRank;
					__shared__ Nd4jLong *zShape;
                    __shared__ Nd4jLong *zStride;

                    if (threadIdx.x == 0) {
				        xRank = shape::rank(xShapeBuffer);
                        xShape = shape::shapeOf(xShapeBuffer);
                        xStride = shape::stride(xShapeBuffer);

					    zRank = shape::rank(zShapeBuffer);
					    zShape = shape::shapeOf(zShapeBuffer);
                        zStride = shape::stride(zShapeBuffer);
				    }
				    __syncthreads();

					Nd4jLong xCoord[MAX_RANK];
					Nd4jLong zCoord[MAX_RANK];

                    for (int e = tid; e < xLength; e += blockDim.x * gridDim.x) {
                        if (xOrder == 'c') {
                            shape::ind2subC(xRank, xShape, e, xCoord);
                            shape::ind2subC(xRank, xShape, sLength - e, zCoord);
                        } else {
                            shape::ind2sub(xRank, xShape, e, xCoord);
                            shape::ind2sub(xRank, xShape, sLength - e, zCoord);
                        }


                        auto xOffset = shape::getOffset(0, xShape, xStride, xCoord, xRank);
                        auto zOffset = shape::getOffset(0, xShape, xStride, zCoord, xRank);

                        result[zOffset] = dx[xOffset];
                    }
                }
            }
		}

#endif


		static void execSpecial(T *dx, Nd4jLong *xShapeBuffer, T *result, Nd4jLong *zShapeBuffer, T *extraParams, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {
			Nd4jLong xLength = shape::length(xShapeBuffer);
			int xEWS = shape::elementWiseStride(xShapeBuffer);
            char xOrder = shape::order(xShapeBuffer);
            Nd4jLong sLength = xLength - 1;

			// two step phase here
			if (dx == result) {
				if (xEWS == 1) {
#pragma omp parallel for schedule(guided)
                    for (Nd4jLong e = 0; e < xLength / 2; e++) {
                        Nd4jLong idx = sLength - e;
                        T tmp = dx[e];
                        dx[e] = dx[idx];
                        dx[idx] = tmp;
                    }
				} else if (xEWS > 1) {
#pragma omp parallel for schedule(guided)
                    for (Nd4jLong e = 0; e < xLength / 2; e++) {
                        Nd4jLong idx1 = (sLength - e) * xEWS;
                        Nd4jLong idx2 =  e * xEWS;
                        T tmp = dx[idx2];
                        dx[idx2] = dx[idx1];
                        dx[idx1] = tmp;
                    }
				} else {
                    int xRank = shape::rank(xShapeBuffer);
                    auto xShape = shape::shapeOf(xShapeBuffer);
                    auto xStride = shape::stride(xShapeBuffer);

                    Nd4jLong xCoord[MAX_RANK];
                    Nd4jLong zCoord[MAX_RANK];

#pragma omp parallel for private(xCoord, zCoord) schedule(guided)
                    for (Nd4jLong e = 0; e < xLength / 2; e++) {
                        if (xOrder == 'c') {
                            shape::ind2subC(xRank, xShape, e, xCoord);
                            shape::ind2subC(xRank, xShape, sLength - e, zCoord);
                        } else {
                            shape::ind2sub(xRank, xShape, e, xCoord);
                            shape::ind2sub(xRank, xShape, sLength - e, zCoord);
                        }

                        auto xOffset = shape::getOffset(0, xShape, xStride, xCoord, xRank);
                        auto zOffset = shape::getOffset(0, xShape, xStride, zCoord, xRank);

                        result[zOffset] = dx[xOffset];
                    }
				}
			} else {
				// single step phase here
				auto zEWS = shape::elementWiseStride(zShapeBuffer);
				auto zOrder = shape::order(zShapeBuffer);

				if (xEWS == 1 && zEWS == 1 && xOrder == zOrder) {
#pragma omp parallel for schedule(guided)
					for (Nd4jLong e = 0; e < xLength; e++) {
						result[sLength - e] = dx[e];
					}
				} else if (xEWS >= 1 && zEWS >= 1 && xOrder == zOrder) {
#pragma omp parallel for schedule(guided)
					for (Nd4jLong e = 0; e < xLength; e++) {
						result[(sLength - e) * zEWS] = dx[e * xEWS];
					}
				} else {

					int xRank = shape::rank(xShapeBuffer);
                    auto xShape = shape::shapeOf(xShapeBuffer);
                    auto xStride = shape::stride(xShapeBuffer);

					int zRank = shape::rank(zShapeBuffer);
					auto zShape = shape::shapeOf(zShapeBuffer);
                    auto zStride = shape::stride(zShapeBuffer);

					Nd4jLong xCoord[MAX_RANK];
					Nd4jLong zCoord[MAX_RANK];

#pragma omp parallel for private(xCoord, zCoord) schedule(guided)
					for (Nd4jLong e = 0; e < xLength; e++) {

						if (xOrder == 'c')
							shape::ind2subC(xRank, xShape, e, xCoord);
						else
							shape::ind2sub(xRank, xShape, e, xCoord);

						if (zOrder == 'c')
                            shape::ind2subC(zRank, zShape, (sLength - e), zCoord);
                        else
                        	shape::ind2sub(zRank, zShape, (sLength - e), zCoord);

						auto xOffset = shape::getOffset(0, xShape, xStride, xCoord, xRank);
                        auto zOffset = shape::getOffset(0, zShape, zStride, zCoord, zRank);

						result[zOffset] = dx[xOffset];
					}
				}
			}
		}

        op_def static T op(T d1, T *params) {
            return d1;
        }
	};

	template<typename T>
	class SoftMax {
	public:
		static const bool requiresSpecial = true;

#ifdef __CUDACC__
		/**
		*
		*/

		static inline __device__ void execSpecialCuda(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams,
			int *allocationPointer, T *reductionPointer, UnifiedSharedMemory *manager, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {

			auto shape = shape::shapeOf(xShapeBuffer);
			__shared__ T maxResult;
			__shared__ Nd4jLong *maxResultShapeBuffer;

			auto length = shape::length(xShapeBuffer);

			auto stride = shape::stride(xShapeBuffer);
			//compute the row wise maxes

			__shared__ Nd4jLong maxShape[2];

			// it's always 2d here
			__shared__ Nd4jLong tempBuffer[8];

			if (threadIdx.x == 0) {
			    maxResult = (T) 0.0;
			    maxShape[0] = shape[0];
			    maxShape[1] = 1;
				maxResultShapeBuffer = shape::shapeBuffer(2, maxShape, tempBuffer);
			}
			__syncthreads();

			functions::reduce::ReduceFunction<T>::template execScalarCuda<simdOps::Max<T>>(dx, xShapeBuffer, extraParams, &maxResult, maxResultShapeBuffer, reductionPointer, manager, nullptr);
			__syncthreads();

			//subtract max of each row
			functions::scalar::ScalarTransform<T>::template transformCuda<simdOps::Subtract<T>>(maxResult, dx, xShapeBuffer, extraParams, result, resultShapeBuffer, allocationPointer, manager);
			__syncthreads();

			//after subtracting the row wise maxes take the exp
			functions::transform::Transform<T>::template transformCuda<simdOps::Exp<T>>(result, resultShapeBuffer, extraParams, result, resultShapeBuffer, allocationPointer, reductionPointer, manager, tadShapeInfo, tadOffsets);
			__syncthreads();

			//take the sum for the exponential
			functions::reduce::ReduceFunction<T>::template execScalarCuda<simdOps::Sum<T>>(result, resultShapeBuffer, extraParams, &maxResult, maxResultShapeBuffer, reductionPointer, manager, nullptr);
			__syncthreads();

			//divide by the sum
			functions::scalar::ScalarTransform<T>::template transformCuda<simdOps::Divide<T>>(maxResult, result, resultShapeBuffer, extraParams, result, resultShapeBuffer, allocationPointer, manager);

		}
#endif

		static void execSpecial(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {
			if (shape::isMatrix(xShapeBuffer)) {
				auto shape = shape::shapeOf(xShapeBuffer);
				//iterate along rows
				int dimension[1] = { 0 };
				int maxDimension[1] = { 1 };
				//compute the row wise maxes
				std::vector<T> maxResult(shape[0]);
				for (int i = 0; i < shape[0]; i++)
					maxResult[i] = 0.0;
				Nd4jLong maxShape[2] = { shape[0], 1 };
				auto maxResultShapeBuffer = shape::shapeBuffer(2, maxShape);
				functions::reduce::ReduceFunction<T>::template exec<simdOps::Max<T>>(dx, xShapeBuffer, extraParams, maxResult.data(), maxResultShapeBuffer, maxDimension, 1,
					nullptr, nullptr);

				//subtract max of each row
				functions::broadcast::Broadcast<T>::template exec<simdOps::Subtract<T>>(dx, xShapeBuffer, maxResult.data(), maxResultShapeBuffer, result, resultShapeBuffer, dimension, 1,
					nullptr, nullptr, nullptr, nullptr);

				//after subtracting the row wise maxes take the exp
				functions::transform::Transform<T>::template exec<simdOps::Exp<T>>(result, resultShapeBuffer, result, resultShapeBuffer, extraParams, tadShapeInfo, tadOffsets);

				//take the sum for the exponential
				functions::reduce::ReduceFunction<T>::template exec<simdOps::Sum<T>>(result, resultShapeBuffer, extraParams, maxResult.data(), maxResultShapeBuffer, maxDimension, 1,
					nullptr, nullptr);

				//divide by the sum
				functions::broadcast::Broadcast<T>::template exec<simdOps::Divide<T>>(result, resultShapeBuffer, maxResult.data(), maxResultShapeBuffer, result, resultShapeBuffer, dimension, 1,
					nullptr, nullptr, nullptr, nullptr);

				delete[] maxResultShapeBuffer;
			}
			else if (shape::isVector(xShapeBuffer)) {
				T max = -FLOAT_MAX_VALUE;
				T sum = 0;
				int elementWiseStride = shape::elementWiseStride(xShapeBuffer);
				int resultElementWiseStride = shape::elementWiseStride(resultShapeBuffer);
				int length = shape::length(xShapeBuffer);
				if (elementWiseStride >= 1 && resultElementWiseStride >= 1) {
					if (elementWiseStride == 1 && resultElementWiseStride == 1) {

#pragma omp simd reduction(maxT:max)
						for (int i = 0; i < length; i++) {
							max = nd4j::math::nd4j_max<T>(max, dx[i]);
						}

#pragma omp parallel for simd reduction(sumT:sum)
						for (int i = 0; i < length; i++) {
                            result[i] = nd4j::math::nd4j_exp<T>(dx[i] - max);
							sum += result[i];
						}

#pragma omp simd
						for (int i = 0; i < length; i++) {
							result[i] /= sum;
						}
					}
					else {

#pragma omp simd reduction(maxT:max)
						for (int i = 0; i < length; i++) {
							max = nd4j::math::nd4j_max<T>(max, dx[i * elementWiseStride]);
						}

#pragma omp parallel for simd reduction(sumT:sum)
						for (int i = 0; i < length; i++) {
                            T r = nd4j::math::nd4j_exp<T>(dx[i * elementWiseStride] - max);
                            result[i * resultElementWiseStride] = r;
							sum += r;
						}

#pragma omp simd
						for (int i = 0; i < length; i++) {
							result[i * resultElementWiseStride] /= sum;
						}
					}
				}
			}
		}

		op_def static T op(T d1, T *params) {
			return nd4j::math::softplus<T>(d1);
		}
	};



	template<typename T>
	class LogSoftMax {
	public:
		static const bool requiresSpecial = true;
#ifdef __CUDACC__
		/**
		*
		*/

		static inline __device__ void execSpecialCuda(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams,
			int *allocationPointer, T *reductionPointer, UnifiedSharedMemory *manager, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {
			auto shape = shape::shapeOf(xShapeBuffer);
			auto stride = shape::stride(xShapeBuffer);
			//iterate along rows

			__shared__ T maxResult;
			__shared__ Nd4jLong *maxResultShapeBuffer;
			if (threadIdx.x == 0) {

				maxResult = (T) 0.0;
			}
			__syncthreads();
			//compute the row wise maxes

			Nd4jLong maxShape[2] = { shape[0], 1 };
			__shared__ Nd4jLong tempBuffer[8];

			if (threadIdx.x == 0)
				maxResultShapeBuffer = shape::shapeBuffer(2, maxShape, tempBuffer);
			__syncthreads();

			functions::reduce::ReduceFunction<T>::template execScalarCuda<simdOps::Max<T>>(dx, xShapeBuffer, extraParams, &maxResult, maxResultShapeBuffer, reductionPointer, manager, nullptr);
			__syncthreads();

			//subtract max of each row
			functions::scalar::ScalarTransform<T>::template transformCuda<simdOps::Subtract<T>>(maxResult, dx, xShapeBuffer, extraParams, result, resultShapeBuffer, allocationPointer, manager);
			__syncthreads();

			//after subtracting the row wise maxes take the exp
			functions::transform::Transform<T>::template transformCuda<simdOps::Exp<T>>(result, resultShapeBuffer, extraParams, result, resultShapeBuffer, allocationPointer, reductionPointer, manager, tadShapeInfo, tadOffsets);
			__syncthreads();

			//take the sum for the exponential
			functions::reduce::ReduceFunction<T>::template execScalarCuda<simdOps::Sum<T>>(result, resultShapeBuffer, extraParams, &maxResult, maxResultShapeBuffer, reductionPointer, manager, nullptr);
			__syncthreads();

			//divide by the sum
			functions::scalar::ScalarTransform<T>::template transformCuda<simdOps::Divide<T>>(maxResult, result, resultShapeBuffer, extraParams, result, resultShapeBuffer, allocationPointer, manager);
			__syncthreads();

			functions::transform::Transform<T>::template transformCuda<simdOps::Log<T>>(result, resultShapeBuffer, extraParams, result, resultShapeBuffer, allocationPointer, reductionPointer, manager, tadShapeInfo, tadOffsets);

		}
#endif


		static void execSpecial(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {

			if (shape::isMatrix(xShapeBuffer, 2)) {
				auto shape = shape::shapeOf(xShapeBuffer);
				//iterate along rows
				int dimension[1] = { 0 };
				int maxDimension[1] = { 1 };
				//compute the row wise maxes
				std::vector <T> maxResult(shape[0]);

#pragma omp simd
				for (int i = 0; i < shape[0]; i++)
					maxResult[i] = 0.0;

				Nd4jLong maxShape[2] = { shape[0], 1 };
				auto maxResultShapeBuffer = shape::shapeBuffer(2, maxShape);
				functions::reduce::ReduceFunction<T>::template exec<simdOps::Max<T>>(dx, xShapeBuffer, extraParams, maxResult.data(), maxResultShapeBuffer, maxDimension, 1,
					nullptr, nullptr);

				//subtract max of each row
				functions::broadcast::Broadcast<T>::template exec<simdOps::Subtract<T>>(dx, xShapeBuffer, maxResult.data(), maxResultShapeBuffer, result, resultShapeBuffer, dimension, 1,
					nullptr, nullptr, nullptr, nullptr);

				//after subtracting the row wise maxes take the exp
				functions::transform::Transform<T>::template exec<simdOps::Exp<T>>(result, resultShapeBuffer, result, resultShapeBuffer, extraParams, tadShapeInfo, tadOffsets);

				//take the sum for the exponential
				functions::reduce::ReduceFunction<T>::template exec<simdOps::Sum<T>>(result, resultShapeBuffer, extraParams, maxResult.data(), maxResultShapeBuffer, maxDimension, 1,
					nullptr, nullptr);

				//divide by the sum
				functions::broadcast::Broadcast<T>::template exec<simdOps::Divide<T>>(result, resultShapeBuffer, maxResult.data(), maxResultShapeBuffer, result, resultShapeBuffer, dimension, 1,
					nullptr, nullptr, nullptr, nullptr);

				functions::transform::Transform<T>::template exec<simdOps::Log<T>>(result, resultShapeBuffer, result, resultShapeBuffer, extraParams, tadShapeInfo, tadOffsets);


				delete[] maxResultShapeBuffer;
			}
			else if (shape::isVector(xShapeBuffer, 2)) {
				T max = -FLOAT_MAX_VALUE;
				T sum = 0;

				int elementWiseStride = shape::elementWiseStride(xShapeBuffer);
				int length = shape::length(xShapeBuffer);
				if (elementWiseStride == 1) {
#pragma omp simd reduction(maxT:max)
					for (int i = 0; i < length; i++) {
						max = nd4j::math::nd4j_max<T>(max, result[i]);
					}

#pragma omp simd reduction(sumT:sum)
					for (int i = 0; i < length; i++) {
						result[i] = nd4j::math::nd4j_exp<T>(dx[i] - max);
						sum += result[i];
					}

#pragma omp simd
					for (int i = 0; i < length; i++) {
						result[i] /= sum;
						result[i] = nd4j::math::nd4j_log<T>(result[i]);
					}
				}
				else if (elementWiseStride > 1) {
#pragma omp simd reduction(maxT:max)
					for (int i = 0; i < length; i++) {
						max = nd4j::math::nd4j_max<T>(max, result[i * elementWiseStride]);
					}

#pragma omp simd reduction(sumT:sum)
					for (int i = 0; i < length; i++) {
						result[i * elementWiseStride] = nd4j::math::nd4j_exp<T>(dx[i * elementWiseStride] - max);
						sum += result[i * elementWiseStride];
					}

#pragma omp simd
					for (int i = 0; i < length; i++) {
						result[i * elementWiseStride] /= sum;
						result[i * elementWiseStride] = nd4j::math::nd4j_log<T>(result[i * elementWiseStride]);
					}
				}
			}
		}

		op_def static T op(T d1, T *params) {
			return nd4j::math::softplus<T>(d1);
		}
	};


	/**
	* softmax(x)
	*/
	template<typename T>
	class SoftMaxDerivative {
	public:
		static const bool requiresSpecial = true;

#ifdef __CUDACC__
		/**
		*
		*/

		static inline __device__ void execSpecialCuda(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams,
			int *allocationPointer, T *reductionPointer, UnifiedSharedMemory *manager, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {

			auto shape = shape::shapeOf(xShapeBuffer);
			__shared__ T maxResult;
			__shared__ Nd4jLong *maxResultShapeBuffer;
			__shared__ Nd4jLong resultEWS;

			auto length = shape::length(xShapeBuffer);

			if (threadIdx.x == 0) {
				resultEWS = shape::elementWiseStride(resultShapeBuffer);

				maxResult = (T) 0.0;
			}
			__syncthreads();

			auto tride = shape::stride(xShapeBuffer);
			Nd4jLong maxShape[2] = { shape[0], 1 };

			__shared__ Nd4jLong tempBuffer[8];

			if (threadIdx.x == 0)
				maxResultShapeBuffer = shape::shapeBuffer(2, maxShape, tempBuffer);
			__syncthreads();

			functions::reduce::ReduceFunction<T>::template execScalarCuda<simdOps::Max<T>>(dx, xShapeBuffer, extraParams, &maxResult, maxResultShapeBuffer, reductionPointer, manager, nullptr);
			__syncthreads();

			//subtract max of each row
			functions::scalar::ScalarTransform<T>::template transformCuda<simdOps::Subtract<T>>(maxResult, dx, xShapeBuffer, extraParams, result, resultShapeBuffer, allocationPointer, manager);
			__syncthreads();

			//after subtracting the row wise maxes take the exp
			functions::transform::Transform<T>::template transformCuda<simdOps::Exp<T>>(result, resultShapeBuffer, extraParams, result, resultShapeBuffer, allocationPointer, reductionPointer, manager, tadShapeInfo, tadOffsets);
			__syncthreads();

			//take the sum for the exponential
			functions::reduce::ReduceFunction<T>::template execScalarCuda<simdOps::Sum<T>>(result, resultShapeBuffer, extraParams, &maxResult, maxResultShapeBuffer, reductionPointer, manager, nullptr);
			__syncthreads();

			//divide by the sum
			functions::scalar::ScalarTransform<T>::template transformCuda<simdOps::Divide<T>>(maxResult, result, resultShapeBuffer, extraParams, result, resultShapeBuffer, allocationPointer, manager);
			__syncthreads();

			if (resultEWS >= 1) {
				for (int i = threadIdx.x; i < length; i += blockDim.x) {
					result[i * resultEWS] = result[i * resultEWS] * ((T) 1.0 - result[i * resultEWS]);
				}
			}
			else {
				printf("Non element wise stride not supported right now\n");
			}

		}
#endif


		static void execSpecial(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {
			if (shape::isMatrix(xShapeBuffer, 2)) {
				auto shape = shape::shapeOf(xShapeBuffer);

				auto resultEleStide = shape::elementWiseStride(resultShapeBuffer);

				//iterate along rows
				int dimension[1] = { 0 };
				int maxDimension[1] = { 1 };
				int len = shape::length(xShapeBuffer);
				//compute the row wise maxes
				std::vector <T> maxResult(shape[0]);
#pragma omp simd
				for (int i = 0; i < shape[0]; i++)
					maxResult[i] = 0.0;

				Nd4jLong maxShape[2] = { shape[0], 1 };
				auto maxResultShapeBuffer = shape::shapeBuffer(2, maxShape);
				functions::reduce::ReduceFunction<T>::template exec<simdOps::Max<T>>(dx, xShapeBuffer, extraParams, maxResult.data(), maxResultShapeBuffer, maxDimension, 1,
					nullptr, nullptr);

				//subtract max of each row
				functions::broadcast::Broadcast<T>::template exec<simdOps::Subtract<T>>(result, resultShapeBuffer, maxResult.data(), maxResultShapeBuffer, result, resultShapeBuffer, dimension, 1,
					nullptr, nullptr, nullptr, nullptr);

				//after subtracting the row wise maxes take the exp
				functions::transform::Transform<T>::template exec<simdOps::Exp<T>>(result, resultShapeBuffer, result, resultShapeBuffer, extraParams, tadShapeInfo, tadOffsets);

				//take the sum for the exponential
				functions::reduce::ReduceFunction<T>::template exec<simdOps::Sum<T>>(result, resultShapeBuffer, extraParams, maxResult.data(), maxResultShapeBuffer, maxDimension,
					1, nullptr, nullptr);

				//divide by the sum
				functions::broadcast::Broadcast<T>::template exec<simdOps::Divide<T>>(result, resultShapeBuffer, maxResult.data(), maxResultShapeBuffer, result, resultShapeBuffer, dimension, 1, nullptr, nullptr, nullptr, nullptr);

				if (resultEleStide >= 1) {
					if (resultEleStide == 1) {
#pragma omp simd
						for (int i = 0; i < len; i++) {
							result[i] = result[i] * ((T) 1.0f - result[i]);
						}

					}
					else {
#pragma omp simd
						for (int i = 0; i < len; i++) {
							result[i * resultEleStide] = result[i * resultEleStide] * ((T) 1.0f - result[i * resultEleStide]);
						}

					}
				}
				else {
                    auto zShape = shape::shapeOf(resultShapeBuffer);
                    auto zStride = shape::stride(resultShapeBuffer);
                    int zRank = shape::rank(resultShapeBuffer);

                    Nd4jLong zCoord[MAX_RANK];

                    for (int i = 0; i < len; i++) {
                        shape::ind2subC(zRank,zShape, i, zCoord);
                        Nd4jLong zOffset = shape::getOffset(0, zShape, zStride, zCoord, zRank);
                        result[zOffset] = result[zOffset] * ((T) 1.0f - result[zOffset]);
                    }
                }


				delete[] maxResultShapeBuffer;
			}
			else if (shape::isVector(xShapeBuffer, 2)) {
				T max = -FLOAT_MAX_VALUE;
				T sum = 0;

				auto elementWiseStride = shape::elementWiseStride(xShapeBuffer);
				auto length = shape::length(xShapeBuffer);
				if (elementWiseStride == 1) {

#pragma omp simd reduction(maxT:max)
					for (int i = 0; i < length; i++) {
						max = nd4j::math::nd4j_max<T>(max, result[i]);
					}

#pragma omp simd reduction(sumT:sum)
					for (int i = 0; i < length; i++) {
						result[i] -= max;
						result[i] = nd4j::math::nd4j_exp<T>(result[i]);
						sum += result[i];
					}

#pragma omp simd
					for (int i = 0; i < length; i++) {
						result[i] /= sum;
					}

#pragma omp simd
                    for (int i = 0; i < length; i++) {
                        result[i] = result[i] * ((T) 1.0f - result[i]);
                    }
                } else if (elementWiseStride >= 1) {

#pragma omp simd reduction(maxT:max)
					for (int i = 0; i < length; i++) {
						max = nd4j::math::nd4j_max<T>(max, result[i * elementWiseStride]);
					}


#pragma omp simd reduction(sumT:sum)
					for (int i = 0; i < length; i++) {
						result[i * elementWiseStride] -= max;
						result[i * elementWiseStride] = nd4j::math::nd4j_exp<T>(result[i * elementWiseStride]);
						sum += result[i * elementWiseStride];
					}

#pragma omp simd
					for (int i = 0; i < length; i++) {
						result[i * elementWiseStride] /= sum;
					}

#pragma omp simd
					for (int i = 0; i < length; i++) {
						result[i * elementWiseStride] = result[i * elementWiseStride] * ((T) 1.0f - result[i * elementWiseStride]);
					}
				} else {
                    printf("non-ews access on row not implemented yet");
                }
			}
		}

		op_def static T op(T d1, T *params) {
			return nd4j::math::softplus<T>(d1);
		}
	};


	template<typename T>
	class IsMax {
	public:
		static const bool requiresSpecial = true;


#ifdef __CUDACC__

		static inline  __device__ void doAllCuda(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams,
			int *allocationPointer, T *reductionPointer, UnifiedSharedMemory *manager) {
// this code is safe to delete, it's never used
/*
			__shared__ int maxIdx;
			__shared__ int length;
			if (threadIdx.x == 0) {
				length = shape::length(resultShapeBuffer);
			}
			__syncthreads();

			functions::indexreduce::IndexReduce<T>::template transform<simdOps::IndexMax<T>>(
				dx,
				xShapeBuffer,
				extraParams,
				result,
				resultShapeBuffer,
				nullptr,
				1,
				1, allocationPointer, reductionPointer, manager, nullptr, nullptr);

			__syncthreads();
			if (threadIdx.x == 0)
				maxIdx = (int)result[0];
			__syncthreads();

			for (int i = threadIdx.x; i < length; i += blockDim.x)
				result[i] = 0;
			__syncthreads();

			if (threadIdx.x == 0) {
				result[maxIdx] = 1.0;
			}
			*/
		}
#endif

#ifdef __CUDACC__
		inline __host__

#elif defined(__GNUC__)


#endif
		static void doAll(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams) {

			auto length = shape::length(xShapeBuffer);
			auto eleStride = shape::elementWiseStride(xShapeBuffer);
			auto resultEleStride = shape::elementWiseStride(resultShapeBuffer);
			auto xOrder = shape::order(xShapeBuffer);
			auto resultOrder = shape::order(resultShapeBuffer);
/*
			int tadsPerThread = tads / TAD_THRESHOLD;
			int num_threads = nd4j::math::nd4j_max<int>(1, tadsPerThread);
			num_threads = nd4j::math::nd4j_min<int>(num_threads, omp_get_max_threads());
*/
			if (xOrder == resultOrder && xOrder == 'c') {
				if (eleStride == 1 && resultEleStride == 1) {
					if (length < ELEMENT_THRESHOLD) {
						int maxIdx = 0;
						T currMax = dx[0];
//#pragma omp simd reduction (max:maxIdx,currMax)
						for (int i = 0; i < length; i++) {
							if (currMax < dx[i]) {
								currMax = dx[i];
								maxIdx = i;
							}

							result[i] = 0.0;

						}

						result[maxIdx] = 1.0;

					}
					else {
						int maxIdx = 0;
						T currMax = dx[0];

#pragma omp parallel proc_bind(AFFINITY)
{
						int maxIdxLocal = maxIdx;
						T currMaxLocal = currMax;

//#pragma omp simd reduction(max:maxIdxLocal,currMaxLocal)
						for (int i = 0; i < length; i++) {
							if (currMaxLocal < dx[i]) {
								currMaxLocal = dx[i];
								maxIdxLocal = i;
							}
							result[i] = 0.0;
						}
#pragma omp critical
{
						if (currMax < currMaxLocal) {
							currMax = currMaxLocal;
							maxIdx = maxIdxLocal;
						}
}
}
						result[maxIdx] = 1.0;
					}

				}
				else {
					if (length < ELEMENT_THRESHOLD) {
						int maxIdx = 0;
						T currMax = dx[0];
//#pragma omp simd reduction(max:maxIdx,currMax)
						for (int i = 0; i < length; i++) {
							result[i * resultEleStride] = 0.0;
							if (currMax < dx[i * eleStride]) {
								currMax = dx[i * eleStride];
								maxIdx = i;
							}
						}

						result[maxIdx * resultEleStride] = 1.0;

					}
					else {
						int maxIdx = 0;
						T currMax = dx[0];

#pragma omp parallel proc_bind(AFFINITY) default(shared)
{
						int maxIdxLocal = maxIdx;
						T currMaxLocal = currMax;
//#pragma omp simd reduction(max:maxIdxLocal,currMaxLocal)
						for (int i = 0; i < length; i++) {
							result[i * resultEleStride] = 0.0;
							if (currMaxLocal < dx[i * eleStride]) {
								currMaxLocal = dx[i * eleStride];
								maxIdxLocal = i;
							}
						}

#pragma omp critical
{
						if (currMax < currMaxLocal) {
							currMax = currMaxLocal;
							maxIdx = maxIdxLocal;
						}
}
}
						result[maxIdx * resultEleStride] = 1.0;
					}

				}
			}


			else {
				Nd4jLong shapeIter[MAX_RANK];
				Nd4jLong coord[MAX_RANK];
				int dim;
				Nd4jLong xStridesIter[MAX_RANK];
				Nd4jLong resultStridesIter[MAX_RANK];
				auto xShape = shape::shapeOf(xShapeBuffer);
				auto xStride = shape::stride(xShapeBuffer);
				auto resultStride = shape::stride(resultShapeBuffer);
				int rank = shape::rank(xShapeBuffer);
				T *originalResult = result;
				if (PrepareTwoRawArrayIter<T>(rank,
					xShape,
					dx,
					xStride,
					result,
					resultStride,
					&rank,
					shapeIter,
					&dx,
					xStridesIter,
					&result,
					resultStridesIter) >= 0) {
					T value = dx[0];
					int idx = 0;
					int maxIdx = 0;
					ND4J_RAW_ITER_START(dim, rank, coord, shapeIter); {
						if (dx[0] > value) {
							value = dx[0];
							maxIdx = idx;
						}

						idx++;
						result[0] = 0.0;

					}
					ND4J_RAW_ITER_TWO_NEXT(
						dim,
						rank,
						coord,
						shapeIter,
						dx,
						xStridesIter,
						result,
						resultStridesIter);

					//pointer to where max value would be
					if (shape::order(resultShapeBuffer) == 'c' || (shape::order(resultShapeBuffer) == 'f' &&
						maxIdx * shape::stride(resultShapeBuffer)[shape::rank(resultShapeBuffer) - 1] >=
						shape::length(resultShapeBuffer)))
						originalResult[maxIdx] = 1.0;
					else
						originalResult[maxIdx * shape::stride(resultShapeBuffer)[shape::rank(resultShapeBuffer) - 1]] = 1.0;
				}
			}


		}
	public:


#ifdef __CUDACC__
		/**
		*
		*/

		static inline __device__ void execSpecialCuda(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, int *allocationPointer, T *reductionPointer, UnifiedSharedMemory *manager, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {
			// FIXME: MAX_DIMENSION is lower then FP16 frame
			if (extraParams == nullptr || (int) extraParams[0] == MAX_DIMENSION) {
				doAllCuda(dx, xShapeBuffer, result, resultShapeBuffer, extraParams, allocationPointer, reductionPointer, manager);
			}
		}
#endif

		static void execSpecial(
			T *dx,
			Nd4jLong *xShapeBuffer,
			T *result,
			Nd4jLong *resultShapeBuffer,
			T *extraParams, Nd4jLong *tadShapeInfo, Nd4jLong *tadOffsets) {
			//FIXME: this op should be moved to CustomOps
			if (extraParams == nullptr || (int)extraParams[0] == 0 ||
				((int)extraParams[0] == 1 && (int)extraParams[1] == MAX_DIMENSION)) {
				doAll(dx, xShapeBuffer, result, resultShapeBuffer, extraParams);
			}
			else if (shape::isVector(xShapeBuffer)) {
				int dimensionLength = (int)extraParams[0];
				int *dimension = new int[dimensionLength];
				int length = shape::length(xShapeBuffer);
				for (int i = 0; i < dimensionLength; i++) {
					dimension[i] = (int)extraParams[i + 1];
				}
				if (shape::shapeOf(xShapeBuffer)[dimension[0]] == 1) {
					for (int i = 0; i < length; i++) {
						result[i] = 1.0;
					}
				}
				else {
					int eleStride = shape::elementWiseStride(xShapeBuffer);
					if (eleStride == 1) {
						int maxIdx = 0;
						T currMax = dx[0];
						if (length < ELEMENT_THRESHOLD) {

//#pragma omp simd reduction(max:maxIdx,currMax)
							for (int i = 0; i < length; i++) {
								if (currMax < dx[i]) {
									currMax = dx[i];
									maxIdx = i;
								}

								result[i] = 0.0;

							}
						}
						else {
#pragma omp parallel proc_bind(AFFINITY) default(shared)
{
							int maxIdxLocal = maxIdx;
							T currMaxLocal = currMax;
//#pragma omp simd reduction(max:maxIdxLocal,currMaxLocal)
							for (int i = 0; i < length; i++) {
								if (currMaxLocal < dx[i]) {
									currMaxLocal = dx[i];
									maxIdxLocal = i;
								}

								result[i] = 0.0;

							}
#pragma omp critical
                            {
							    if (currMax < currMaxLocal) {
								    currMax = currMaxLocal;
								    maxIdx = maxIdxLocal;
							    }
                            }
}
						}

						result[maxIdx] = 1.0;

					}


					else {
						int maxIdx = 0;
						T currMax = dx[0];
						if (length < ELEMENT_THRESHOLD) {
//#pragma omp parallel for reduction(max:maxIdx,currMax) proc_bind(AFFINITY)
							for (int i = 0; i < length; i++) {
								if (currMax < dx[i * eleStride]) {
									currMax = dx[i * eleStride];
									maxIdx = i;
								}

								result[i] = 0.0;
							}
						}
						else {
#pragma omp parallel proc_bind(AFFINITY) default(shared)
{
							int maxIdxLocal = maxIdx;
							T currMaxLocal = currMax;

//#pragma omp parallel for reduction(max:maxIdx,currMax)  proc_bind(AFFINITY)
							for (int i = 0; i < length; i++) {
								if (currMaxLocal < dx[i * eleStride]) {
									currMaxLocal = dx[i * eleStride];
									maxIdxLocal = i;
								}

								result[i] = 0.0;
							}
#pragma omp critical
{
							if (currMax < currMaxLocal) {
								currMax = currMaxLocal;
								maxIdx = maxIdxLocal;
							}
}
}
						}

						result[maxIdx] = 1.0;
					}
				}


			}
			else {
                int dimensionLength = (int) extraParams[0];
                int *dimension = new int[dimensionLength];

#pragma omp simd
                for (int i = 0; i < dimensionLength; i++) {
                    dimension[i] = (int) extraParams[i + 1];
                }
                //decompose in to several sub tads after
                //moving all dimensions (in sorted order)
                //to the back.
                //permuted version of the x shape info for setting up the tad problem				
				auto tadShapeShapeInfo = tadShapeInfo;
				shape::TAD tad (xShapeBuffer, dimension, dimensionLength);
				if(tadShapeInfo==nullptr) {
					tad.createTadOnlyShapeInfo();
					tad.createOffsets();
					tadShapeShapeInfo = tad.tadOnlyShapeInfo;
					tadOffsets = tad.tadOffsets;
				}						                                				

                auto tadLength = shape::tadLength(xShapeBuffer, dimension, dimensionLength);
                auto tads = shape::length(xShapeBuffer) / tadLength;

                int tadsPerThread = tads / TAD_THRESHOLD;
                int num_threads = nd4j::math::nd4j_max<int>(1, tadsPerThread);
                num_threads = nd4j::math::nd4j_min<int>(num_threads, omp_get_max_threads());

                auto tadEWS = shape::elementWiseStride(tadShapeShapeInfo);
                auto zEWS = tadEWS;

                int span = (tads / num_threads) + 8;

#pragma omp parallel num_threads(num_threads) if (num_threads>1) proc_bind(AFFINITY)
                {
                    int tid = omp_get_thread_num();
                    int start = span * tid;
                    int end = span * (tid + 1);
                    if (end > tads) end = tads;

                    for (int r = start; r < end; r++) {
                        if (tadEWS > 0 && zEWS > 0 && dimensionLength == 1) {
                            T *rX = dx + tadOffsets[r];
                            T *rZ = result + tadOffsets[r];

                            T maxValue = rX[0];
                            int maxIdx = 0;
                            if (tadEWS == 1 && zEWS == 1) {
//#pragma omp simd reduction(max:maxValue,maxIdx)
                                for (int i = 0; i < tadLength; i++) {
                                    if (rX[i] > maxValue) {
                                        maxIdx = i;
                                        maxValue = rX[i];
                                    }
                                }

#pragma omp simd
                                for (int i = 0; i < tadLength; i++) {
                                    rZ[i] = maxIdx == i ? (T) 1.0 : (T) 0.0;
                                }

                            } else {

//#pragma omp parallel for reduction(max:maxValue,maxIdx) default(shared)
                                for (int i = 0; i < tadLength; i++) {
                                    if (rX[i * tadEWS] > maxValue) {
                                        maxIdx = i;
                                        maxValue = rX[i * tadEWS];
                                    }
                                }

#pragma omp simd
                                for (int i = 0; i < tadLength; i++) {
                                    rZ[i * zEWS] = maxIdx == i ? (T) 1.0 : (T) 0.0;
                                }
                            }
                        } else {
                            int tadsPerThread = tads / TAD_THRESHOLD;
                            int num_threads = nd4j::math::nd4j_max<int>(1, tadsPerThread);
                            num_threads = nd4j::math::nd4j_min<int>(num_threads, omp_get_max_threads());

                            auto offset = tadOffsets[r];
                            Nd4jLong shapeIter[MAX_RANK];
                            Nd4jLong coord[MAX_RANK];
                            int dim;
                            Nd4jLong xStridesIter[MAX_RANK];
                            Nd4jLong resultStridesIter[MAX_RANK];
                            auto xShape = shape::shapeOf(tadShapeShapeInfo);
                            auto xStride = shape::stride(tadShapeShapeInfo);
                            auto resultStride = shape::stride(tadShapeShapeInfo);
                            int rank = shape::rank(tadShapeShapeInfo);
                            T *xPointer = dx + offset;
                            T *resultPointer = result + offset;
                            T maxValue = xPointer[0];

                            T *maxCursor = resultPointer;
                            Nd4jPointer maxCursorLong = reinterpret_cast<Nd4jPointer>(maxCursor);
                            if (PrepareTwoRawArrayIter<T>(rank,
                                                             xShape,
                                                             xPointer,
                                                             xStride,
                                                             resultPointer,
                                                             resultStride,
                                                             &rank,
                                                             shapeIter,
                                                             &xPointer,
                                                             xStridesIter,
                                                             &resultPointer,
                                                             resultStridesIter) >= 0) {
                                   ND4J_RAW_ITER_START(dim, rank, coord, shapeIter); {
                                       if (maxValue < xPointer[0]) {
                                           maxCursor = resultPointer;
                                           maxCursorLong = reinterpret_cast<Nd4jPointer>(resultPointer);
                                           maxValue = xPointer[0];
                                       }

                                       resultPointer[0] = 0.0;
                                   }
                                   ND4J_RAW_ITER_TWO_NEXT(dim,
                                                          rank,
                                                          coord,
                                                          shapeIter,
                                                          xPointer,
                                                          xStridesIter,
                                                          resultPointer,
                                                          resultStridesIter);
                                   maxCursor = reinterpret_cast<T *>(maxCursorLong);
                                   maxCursor[0] = 1.0;
                            }
                        }
                    }
                }

                delete[] dimension;
            }
		}

		op_def static T op(T d1, T *params) {
			return nd4j::math::softplus<T>(d1);
		}
	};
}
