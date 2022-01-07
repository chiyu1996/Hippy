/* Tencent is pleased to support the open source community by making Hippy available.
 * Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.tencent.mtt.hippy.modules;

@SuppressWarnings({"unused"})
public interface Promise {

    enum BridgeTransferType {
        BRIDGE_TRANSFER_TYPE_NORMAL(0),
        BRIDGE_TRANSFER_TYPE_NIO(1);

        private final int iValue;

        BridgeTransferType(int value) {
            iValue = value;
        }

        public int value() {
            return iValue;
        }
    }

    void resolve(Object value);

    void reject(Object error);

    boolean isCallback();

    String getCallId();

    void setTransferType(BridgeTransferType type);
}