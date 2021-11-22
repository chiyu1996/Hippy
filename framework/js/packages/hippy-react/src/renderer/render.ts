/* eslint-disable no-underscore-dangle */
/* eslint-disable no-param-reassign */

import Hippy from '@localTypes/hippy';
import ViewNode from '../dom/view-node';
import Element from '../dom/element-node';
import * as UIManagerModule from '../modules/ui-manager-module';
import { Device } from '../global';
import { getRootViewId, getRootContainer } from '../utils/node';
import { trace, warn } from '../utils';

const componentName = ['%c[native]%c', 'color: red', 'color: auto'];

interface BatchType {
  [key: string]: Symbol;
}

const NODE_OPERATION_TYPES: BatchType = {
  createNode: Symbol('createNode'),
  updateNode: Symbol('updateNode'),
  deleteNode: Symbol('deleteNode'),
};

interface BatchChunk {
  type: Symbol,
  nodes: Hippy.NativeNode[]
}

let batchIdle: boolean = true;
let batchNodes: BatchChunk[] = [];

/**
 * Convert an ordered node array into multiple fragments
 */
function chunkNodes(batchNodes: BatchChunk[]) {
  const result: BatchChunk[] = [];
  for (let i = 0; i < batchNodes.length; i += 1) {
    const chunk: BatchChunk = batchNodes[i];
    const { type, nodes } = chunk;
    const lastChunk = result[result.length - 1];
    if (!lastChunk || lastChunk.type !== type) {
      result.push({
        type,
        nodes,
      });
    } else {
      lastChunk.nodes = lastChunk.nodes.concat(nodes);
    }
  }
  return result;
}

function startBatch() {
  if (batchIdle) {
    UIManagerModule.startBatch();
  }
}

function endBatch(rootViewId: number) {
  if (!batchIdle) {
    return;
  }
  batchIdle = false;
  Promise.resolve().then(() => {
    const chunks = chunkNodes(batchNodes);
    chunks.forEach((chunk) => {
      switch (chunk.type) {
        case NODE_OPERATION_TYPES.createNode:
          trace(...componentName, 'createNode', chunk.nodes);
          UIManagerModule.createNode(rootViewId, chunk.nodes);
          break;
        case NODE_OPERATION_TYPES.updateNode:
          trace(...componentName, 'updateNode', chunk.nodes);
          // FIXME: iOS should be able to update multiple nodes at once.
          // @ts-ignore
          if (__PLATFORM__ === 'ios' || Device.platform.OS === 'ios') {
            chunk.nodes.forEach(node => (
              UIManagerModule.updateNode(rootViewId, [node])
            ));
          } else {
            UIManagerModule.updateNode(rootViewId, chunk.nodes);
          }
          break;
        case NODE_OPERATION_TYPES.deleteNode:
          trace(...componentName, 'deleteNode', chunk.nodes);
          // FIXME: iOS should be able to delete mutiple nodes at once.
          // @ts-ignore
          if (__PLATFORM__ === 'ios' || Device.platform.OS === 'ios') {
            chunk.nodes.forEach(node => (
              UIManagerModule.deleteNode(rootViewId, [node])
            ));
          } else {
            UIManagerModule.deleteNode(rootViewId, chunk.nodes);
          }
          break;
        default:
          // pass
      }
    });
    UIManagerModule.endBatch();
    batchNodes = [];
    batchIdle = true;
  });
}

/**
 * Translate to native props from attributes and meta
 */
function getNativeProps(node: Element) {
  /* eslint-disable-next-line @typescript-eslint/no-unused-vars */
  const { children, ...otherProps } = node.attributes;
  return otherProps;
}

/**
 * Get target node attributes, used to chrome devTool tag attribute show while debugging
 */
function getTargetNodeAttributes(targetNode: Element) {
  try {
    const targetNodeAttributes = JSON.parse(JSON.stringify(targetNode.attributes));
    const attributes = {
      id: targetNode.id,
      ...targetNodeAttributes,
    };
    delete attributes.text;
    delete attributes.value;
    return attributes;
  } catch (e) {
    warn('getTargetNodeAttributes error:', e);
    return {};
  }
}

/**
 * Render Element to native
 */
function renderToNative(rootViewId: number, targetNode: Element): Hippy.NativeNode | null {
  if (!targetNode.nativeName) {
    warn('Component need to define the native name', targetNode);
    return null;
  }
  if (targetNode.meta.skipAddToDom) {
    return null;
  }
  if (!targetNode.meta.component) {
    throw new Error(`Specific tag is not supported yet: ${targetNode.tagName}`);
  }
  // Translate to native node
  const nativeNode: Hippy.NativeNode = {
    id: targetNode.nodeId,
    pId: (targetNode.parentNode && targetNode.parentNode.nodeId) || rootViewId,
    index: targetNode.index,
    name: targetNode.nativeName,
    props: {
      ...getNativeProps(targetNode),
      style: targetNode.style,
    },
  };
  // Add nativeNode attributes info for debugging
  if (process.env.NODE_ENV !== 'production') {
    nativeNode.tagName = targetNode.nativeName;
    if (nativeNode.props) {
      nativeNode.props.attributes = getTargetNodeAttributes(targetNode);
    }
  }
  return nativeNode;
}

/**
 * Render Element with child to native
 * @param rootViewId - rootView id
 * @param node - current node
 * @param atIndex - current node index
 */
function renderToNativeWithChildren(rootViewId: number, node: ViewNode, atIndex?: number | undefined) {
  const nativeLanguages: Hippy.NativeNode[] = [];
  let index = atIndex;
  if (typeof index === 'undefined' && node && node.parentNode) {
    index = node.parentNode.childNodes.indexOf(node);
  }
  node.traverseChildren((targetNode: Element) => {
    const nativeNode = renderToNative(rootViewId, targetNode);
    if (nativeNode) {
      nativeLanguages.push(nativeNode);
    }
  }, index);
  return nativeLanguages;
}

function isLayout(node: ViewNode) {
  const container = getRootContainer();
  if (!container) {
    return false;
  }
  // Determine node is a Document instance
  return node instanceof container.containerInfo.constructor;
}

/* eslint-disable-next-line @typescript-eslint/no-unused-vars */
function insertChild(parentNode: ViewNode, childNode: ViewNode, atIndex = -1) {
  if (!parentNode || !childNode) {
    return;
  }
  if (childNode.meta.skipAddToDom) {
    return;
  }
  const rootViewId = getRootViewId();
  // Render the root node
  if (isLayout(parentNode) && !parentNode.isMounted) {
    // Start real native work.
    const translated = renderToNativeWithChildren(rootViewId, childNode, atIndex);
    startBatch();
    batchNodes.push({
      type: NODE_OPERATION_TYPES.createNode,
      nodes: translated,
    });
    endBatch(rootViewId);
    parentNode.traverseChildren((node: ViewNode) => {
      if (!node.isMounted) {
        node.isMounted = true;
      }
    }, atIndex);
    // Render others child nodes.
  } else if (parentNode.isMounted && !childNode.isMounted) {
    const translated = renderToNativeWithChildren(rootViewId, childNode, atIndex);
    startBatch();
    batchNodes.push({
      type: NODE_OPERATION_TYPES.createNode,
      nodes: translated,
    });
    endBatch(rootViewId);
    childNode.traverseChildren((node: ViewNode) => {
      if (!node.isMounted) {
        node.isMounted = true;
      }
    }, atIndex);
  }
}

function removeChild(parentNode: ViewNode, childNode: ViewNode | null, index: number) {
  if (!childNode || childNode.meta.skipAddToDom) {
    return;
  }
  childNode.isMounted = false;
  childNode.index = index;
  const rootViewId = getRootViewId();
  const deleteNodeIds: Hippy.NativeNode[] = [{
    id: childNode.nodeId,
    pId: childNode.parentNode ? childNode.parentNode.nodeId : rootViewId,
    index: childNode.index,
  }];
  startBatch();
  batchNodes.push({
    type: NODE_OPERATION_TYPES.deleteNode,
    nodes: deleteNodeIds,
  });
  endBatch(rootViewId);
}

function updateChild(parentNode: Element) {
  if (!parentNode.isMounted) {
    return;
  }
  const rootViewId = getRootViewId();
  const translated = renderToNative(rootViewId, parentNode);
  startBatch();
  if (translated) {
    batchNodes.push({
      type: NODE_OPERATION_TYPES.updateNode,
      nodes: [translated],
    });
  }
  endBatch(rootViewId);
}

function updateWithChildren(parentNode: ViewNode) {
  if (!parentNode.isMounted) {
    return;
  }
  const rootViewId = getRootViewId();
  const translated = renderToNativeWithChildren(rootViewId, parentNode);
  startBatch();
  batchNodes.push({
    type: NODE_OPERATION_TYPES.updateNode,
    nodes: translated,
  });
  endBatch(rootViewId);
}

export {
  renderToNative,
  renderToNativeWithChildren,
  insertChild,
  removeChild,
  updateChild,
  updateWithChildren,
};