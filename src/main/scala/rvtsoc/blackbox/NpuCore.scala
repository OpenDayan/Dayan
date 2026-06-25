package rvtsoc.blackbox

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config.Parameters

import xs.infra.axi.ExtAxiBundle
import xs.infra.axi.AxiParams
import rvtsoc.subsystem.XbarParamsKey
import rvtsoc.subsystem.{BaseNpuCore, BaseRvtngCore}
import rvtsoc.subsystem.BaseScalarCore

class NpuCore(implicit p: Parameters) extends BaseNpuCore {
  io <> DontCare
}

class ScalarCore(implicit p: Parameters) extends BaseScalarCore {
  io <> DontCare
}

class RvtngCore(implicit p: Parameters) extends BaseRvtngCore {
  io <> DontCare
}