export const FLOW_NODE_SIZE = Object.freeze({ width: 76, height: 96 })
export const DETAIL_PANEL_SIZE = Object.freeze({ width: 360, height: 350 })
export const DETAIL_PANEL_GAP = 12

export const getFlowNodeDimensions = () => ({ ...FLOW_NODE_SIZE })

export const getFlowLayoutSpacing = (dimensions = FLOW_NODE_SIZE) => ({
  nodesep: Math.max(40, Math.min(240, Math.round(dimensions.height * 0.5))),
  ranksep: Math.max(50, Math.min(320, Math.round(dimensions.width * 0.4)))
})

export const getDetailPanelAnchor = (node, dimensions = FLOW_NODE_SIZE) => ({
  x: node.position.x + dimensions.width / 2 - DETAIL_PANEL_SIZE.width / 2,
  y: node.position.y + dimensions.height + DETAIL_PANEL_GAP
})
