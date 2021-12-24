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
package com.tencent.renderer;

import java.util.ArrayList;

public interface INativeRenderDelegate {

    /**
     * Create render node
     *
     * @param list {@link ArrayList} The node list array
     */
    void createNode(ArrayList list);

    /**
     * Update render node
     *
     * @param list {@link ArrayList} The node list array
     */
    void updateNode(ArrayList list);

    /**
     * Delete render node
     *
     * @param list {@link ArrayList} The node list array
     */
    void deleteNode(ArrayList list);

    /**
     * Update render node layout
     *
     * @param list {@link ArrayList} The node list array
     */
    void updateLayout(ArrayList list);

    /**
     * Mark will execute node update command
     */
    void startBatch();

    /**
     * Mark all node update command execute completed
     */
    void endBatch();

    /**
     * Report render exception to host
     *
     * @param exception {@link Exception} The render exception
     */
    void handleRenderException(Exception exception);
}