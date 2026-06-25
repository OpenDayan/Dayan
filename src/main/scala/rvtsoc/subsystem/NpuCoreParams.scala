package rvtsoc.subsystem

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config._

case object NpuCoreParamsKey extends Field[Option[NpuCoreParams]](None)

case class NpuCoreParams()
