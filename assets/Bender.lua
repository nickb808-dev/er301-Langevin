-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Nicholas Breinich (nickb808) — see LICENSE and NOTICE.
--
-- Bender — a clock bender, after the ADDAC406. Two benders, A and B, each a
-- pulse train whose tempo glides from the input's to the input x M and back
-- over a cycle counted in input beats, then pauses (rest / hold); every cycle
-- starts on an input beat. Taps (local sources, the Huygens pattern): A, B
-- (triggers), A rate, B rate (1 V/oct CV that follows each curve — into
-- Noether's V/Oct the loop speed bends with the clock).
--
-- Bender's own chain output is the channel's AUDIO, passed through
-- untouched. The taps are its sub-outs after the audio: in any control's
-- source chooser, Locals, highlight Bender, M6 steps through them, Enter
-- picks (stolmine firmware 9.2+, xroot/Source/LocalChooser.lua).
--
-- EVERY app.* / self:* CALL HERE ALREADY APPEARS IN A SHIPPED UNIT'S LUA
-- (Huygens.lua, Noether.lua, ClockBase.lua, MultiBand.lua). Verify with the
-- comm(1) audit in ER301-UNIT-STANDARD.md §1 before every release.

local app = app
local Class = require "Base.Class"
local Unit = require "Unit"
local Encoder = require "Encoder"
local GainBias = require "Unit.ViewControl.GainBias"
local Gate = require "Unit.ViewControl.Gate"
local MenuHeader = require "Unit.MenuControl.Header"
local OptionControl = require "Unit.MenuControl.OptionControl"
local ply = app.SECTION_PLY

local libbender = require "bender.libbender"

-- THE RUNNING VERSION, ON SCREEN (standard §10c). tools/check-version.sh
-- fails the build if this drifts from the Makefile or toc.lua.
local VERSION = "0.1.0"

local kTaps = 4

local Bender = Class {}
Bender:include(Unit)

function Bender:init(args)
  args.title = "Bender"
  args.mnemonic = "Bd"
  -- the chain reads a unit's first output (mono) or first two (stereo): the
  -- audio goes there, the taps follow (read before Unit.init, as MultiBand)
  local chain = args.chain or app.logError("%s.init: chain is missing.", self)
  local audio = (chain and chain.channelCount and chain.channelCount > 1) and 2 or 1
  args.channelCount = audio + kTaps
  local labels = audio == 2 and { "audio L", "audio R" } or { "audio" }
  labels[audio + 1] = "A"
  labels[audio + 2] = "B"
  labels[audio + 3] = "A rate"
  labels[audio + 4] = "B rate"
  args.subOutLabels = labels
  Unit.init(self, args)
  self.audioCount = audio
end

-- One CV-able parameter: a GainBias driving the engine port, a MinMax so the
-- dial knows its range, and a mono branch so it takes CV (Noether's helper).
local function param(self, head, name, port, bias)
  local p = self:addObject(name .. "Param", app.GainBias())
  local r = self:addObject(name .. "Range", app.MinMax())
  p:hardSet("Bias", bias)
  connect(p, "Out", r, "In")
  connect(p, "Out", head, port)
  self:addMonoBranch(name, p, "In", p, "Out")
end

function Bender:onLoadGraph(channelCount)
  local head = self:addObject("head", libbender.Bender())

  -- CLOCK and RESET: Comparators; the engine finds the rising edges itself
  local clk = self:addObject("clk", app.Comparator())
  connect(clk, "Out", head, "Clock")
  self:addMonoBranch("clk", clk, "In", clk, "Out")

  local reset = self:addObject("reset", app.Comparator())
  connect(reset, "Out", head, "Reset")
  self:addMonoBranch("reset", reset, "In", reset, "Out")

  param(self, head, "aMult",  "A Mult",    2.0)
  param(self, head, "aBeats", "A Beats",   8.0)
  param(self, head, "aBal",   "A Balance", 0.5)
  param(self, head, "aLeft",  "A Left",    0.0)
  param(self, head, "aRight", "A Right",   0.0)
  param(self, head, "aPause", "A Pause",   0.0)
  param(self, head, "bMult",  "B Mult",    0.5)
  param(self, head, "bBeats", "B Beats",   8.0)
  param(self, head, "bBal",   "B Balance", 0.5)
  param(self, head, "bLeft",  "B Left",    0.0)
  param(self, head, "bRight", "B Right",   0.0)
  param(self, head, "bPause", "B Pause",   0.0)

  -- the audio, through the engine untouched, on the outputs the chain reads;
  -- then the taps, one literal line each (tools/check-ports.sh verifies them)
  connect(self, "In1", head, "Left In")
  connect(head, "Left Out", self, "Out1")
  if channelCount - kTaps > 1 then
    connect(self, "In2", head, "Right In")
    connect(head, "Right Out", self, "Out2")
    connect(head, "Tap1", self, "Out3")
    connect(head, "Tap2", self, "Out4")
    connect(head, "Tap3", self, "Out5")
    connect(head, "Tap4", self, "Out6")
  else
    connect(head, "Tap1", self, "Out2")
    connect(head, "Tap2", self, "Out3")
    connect(head, "Tap3", self, "Out4")
    connect(head, "Tap4", self, "Out5")
  end
end

-- ── menu ────────────────────────────────────────────────────────────────
local menu = { "header", "howto", "aRun", "aHold", "bRun", "bHold" }

function Bender:onShowMenu(objects, branches)
  local controls = {}
  controls.header = MenuHeader { description = "Bender v" .. VERSION }
  controls.howto = MenuHeader { description = "Taps: Locals, highlight Bd, M6" }
  controls.aRun = OptionControl {
    description = "A runs", option = objects.head:getOption("A Run"),
    choices = { "loop", "one-shot" }, descriptionWidth = 2,
  }
  controls.aHold = OptionControl {
    description = "A pause", option = objects.head:getOption("A Hold"),
    choices = { "rest", "hold" }, descriptionWidth = 2,
  }
  controls.bRun = OptionControl {
    description = "B runs", option = objects.head:getOption("B Run"),
    choices = { "loop", "one-shot" }, descriptionWidth = 2,
  }
  controls.bHold = OptionControl {
    description = "B pause", option = objects.head:getOption("B Hold"),
    choices = { "rest", "hold" }, descriptionWidth = 2,
  }
  return controls, menu
end

-- ── views ───────────────────────────────────────────────────────────────
local controlOrder = { "clk", "reset",
  "aMult", "aBeats", "aBal", "aLeft", "aRight", "aPause",
  "bMult", "bBeats", "bBal", "bLeft", "bRight", "bPause" }
local views = { expanded = { "curve" }, collapsed = {} }
for _, name in ipairs(controlOrder) do
  views.expanded[#views.expanded + 1] = name
  views[name] = { "curve", name }
end

function Bender:onLoadViews(objects, branches)
  local controls = {}

  local CurveView = require "bender.CurveView"
  controls.curve = CurveView { name = "curve", head = objects.head, width = 2 * ply }

  controls.clk = Gate { button = "clk", description = "clock in",
                        branch = branches.clk, comparator = objects.clk }
  controls.reset = Gate { button = "reset", description = "reset: both restart on the next beat",
                          branch = branches.reset, comparator = objects.reset }

  local function gb(name, btn, desc, map, bias, precision)
    controls[name] = GainBias {
      button = btn, description = desc, branch = branches[name],
      gainbias = objects[name .. "Param"], range = objects[name .. "Range"],
      biasMap = map, initialBias = bias, biasPrecision = precision,
    }
  end
  for _, ch in ipairs({ "a", "b" }) do
    local C = ch == "a" and "A" or "B"
    -- x M: 0.25 .. 8, a coarse click = 0.25 (31 steps), as Noether's speed dial
    local multMap = app.LinearDialMap(0.25, 8); multMap:setCoarseRadix(31)
    -- whole beats, as Noether's bars dial
    local beatsMap = app.LinearDialMap(1, 64); beatsMap:setRounding(1); beatsMap:setCoarseRadix(63)
    local pauseMap = app.LinearDialMap(0, 32); pauseMap:setRounding(1); pauseMap:setCoarseRadix(32)
    gb(ch .. "Mult",  C .. " mult", C .. ": tempo goes to input x M (below 1 slows)", multMap, ch == "a" and 2.0 or 0.5, 2)
    gb(ch .. "Beats", C .. " beat", C .. ": beats to get there and back", beatsMap, 8, 0)
    gb(ch .. "Bal",   C .. " bal",  C .. ": 0 down only .. 1 up only", Encoder.getMap("[0,1]"), 0.5, 2)
    gb(ch .. "Left",  C .. " left", C .. ": going-out curve (- slow start, + fast)", Encoder.getMap("[-1,1]"), 0.0, 2)
    gb(ch .. "Right", C .. " rght", C .. ": coming-back curve", Encoder.getMap("[-1,1]"), 0.0, 2)
    gb(ch .. "Pause", C .. " paus", C .. ": beats of pause after (menu: rest / hold)", pauseMap, 0, 0)
  end

  return controls, views
end

return Bender
