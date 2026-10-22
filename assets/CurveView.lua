-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Nicholas Breinich (nickb808) — see LICENSE and NOTICE.
--
-- CurveView.lua — display-only ViewControl: the two tempo curves, and on
-- the sub display the input tempo and each bender's tempo and beat. The same shape as
-- Noether's ReelView and Dirac's GrainFieldView.

local app = app
local Class = require "Base.Class"
local ViewControl = require "Unit.ViewControl"
local libbender = require "bender.libbender"
local ply = app.SECTION_PLY

local CurveView = Class {}
CurveView:include(ViewControl)

function CurveView:init(args)
  ViewControl.init(self, args.name or "curve")
  self:setClassName("Bender.CurveView")
  local head = args.head or app.logError("%s.init: head is missing.", self)
  local width = args.width or (2 * ply)

  self.curve = libbender.BenderCurve(0, 0, width, 64)
  self.curve:follow(head)

  local graphic = app.Graphic(0, 0, width, 64)
  graphic:addChild(self.curve)
  self:setControlGraphic(graphic)
  self:setMainCursorController(self.curve)

  for i = 1, (width // ply) do
    self:addSpotDescriptor{ center = (i - 0.5) * ply }
  end

  self.info = libbender.BenderInfo(0, 0, 128, 64)
  self.info:follow(head)
  self.subGraphic = app.Graphic(0, 0, 128, 64)
  self.subGraphic:addChild(self.info)
end

return CurveView
