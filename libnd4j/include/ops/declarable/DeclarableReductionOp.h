/*******************************************************************************
 * Copyright (c) 2015-2018 Skymind, Inc.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License, Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/

//
// Created by raver119 on 07.10.2017.
//

#ifndef LIBND4J_DECLARABLE_REDUCTION_OP_H
#define LIBND4J_DECLARABLE_REDUCTION_OP_H

#include <ops/declarable/DeclarableOp.h>

namespace nd4j {
    namespace ops {
        class ND4J_EXPORT DeclarableReductionOp : public nd4j::ops::DeclarableOp {
        protected:
            /**
             * This method executes this Op
             */
            Nd4jStatus validateAndExecute(Context& block) override = 0;
        public:
            DeclarableReductionOp(int numInputs, int numOutputs, const char *opName, bool allowsInplace, int tArgs, int iArgs);
            ~DeclarableReductionOp();

            ShapeList* calculateOutputShape(ShapeList* inputShape, nd4j::graph::Context& block) override;
        };
    }
}

#endif //LIBND4J_DECLARABLE_REDUCTION_OP_H
