package rvtsoc.subsystem

import chisel3._

import freechips.rocketchip.amba.axi4.AXI4Bundle
import freechips.rocketchip.util.AsyncQueueParams

import lmss.axi.{
  AxiBundle => LmAxiBundle,
  ExtAxiBundle => LmExtAxiBundle,
  AxiParams => LmAxiParams
}
import lmss.param.PortParams
import rvtng.utils.PartialAssign

object utils {
  def connectAXI4BundleAxiBundle(
      axi4Bundle: AXI4Bundle,
      axiBundle: LmAxiBundle
  ) = {
    // ar channel
    axi4Bundle.ar.bits.addr := axiBundle.ar.bits.addr
    axi4Bundle.ar.bits.id := axiBundle.ar.bits.id
    axi4Bundle.ar.bits.len := axiBundle.ar.bits.len
    axi4Bundle.ar.bits.size := axiBundle.ar.bits.size
    axi4Bundle.ar.bits.burst := axiBundle.ar.bits.burst
    axi4Bundle.ar.bits.lock := axiBundle.ar.bits.lock
    axi4Bundle.ar.bits.cache := axiBundle.ar.bits.cache
    axi4Bundle.ar.bits.prot := axiBundle.ar.bits.prot
    axi4Bundle.ar.bits.qos := axiBundle.ar.bits.qos
    axi4Bundle.ar.valid := axiBundle.ar.valid
    axiBundle.ar.ready := axi4Bundle.ar.ready

    // aw channel
    axi4Bundle.aw.bits.addr := axiBundle.aw.bits.addr
    axi4Bundle.aw.bits.id := axiBundle.aw.bits.id
    axi4Bundle.aw.bits.len := axiBundle.aw.bits.len
    axi4Bundle.aw.bits.size := axiBundle.aw.bits.size
    axi4Bundle.aw.bits.burst := axiBundle.aw.bits.burst
    axi4Bundle.aw.bits.lock := axiBundle.aw.bits.lock
    axi4Bundle.aw.bits.cache := axiBundle.aw.bits.cache
    axi4Bundle.aw.bits.prot := axiBundle.aw.bits.prot
    axi4Bundle.aw.bits.qos := axiBundle.aw.bits.qos
    axi4Bundle.aw.valid := axiBundle.aw.valid
    axiBundle.aw.ready := axi4Bundle.aw.ready

    // w channel
    axi4Bundle.w.bits.data := axiBundle.w.bits.data
    axi4Bundle.w.bits.strb := axiBundle.w.bits.strb
    axi4Bundle.w.bits.last := axiBundle.w.bits.last
    axi4Bundle.w.valid := axiBundle.w.valid
    axiBundle.w.ready := axi4Bundle.w.ready

    // b channel
    axiBundle.b.bits.id := axi4Bundle.b.bits.id
    axiBundle.b.bits.resp := axi4Bundle.b.bits.resp
    axiBundle.b.bits.user := DontCare
    axiBundle.b.valid := axi4Bundle.b.valid
    axi4Bundle.b.ready := axiBundle.b.ready

    // r channel
    axiBundle.r.bits.id := axi4Bundle.r.bits.id
    axiBundle.r.bits.data := axi4Bundle.r.bits.data
    axiBundle.r.bits.last := axi4Bundle.r.bits.last
    axiBundle.r.bits.resp := axi4Bundle.r.bits.resp
    axiBundle.r.bits.user := DontCare
    axiBundle.r.valid := axi4Bundle.r.valid
    axi4Bundle.r.ready := axiBundle.r.ready
  }

  def connectAxi(axi4Bundle: AXI4Bundle, axiBundle: LmAxiBundle) =
    connectAXI4BundleAxiBundle(axi4Bundle, axiBundle)

  def connectAxi(axi4Bundle: AXI4Bundle, extAxiBundle: LmExtAxiBundle) {
    val axiBundle = Wire(new LmAxiBundle(extAxiBundle.params))
    axiBundle <> extAxiBundle
    connectAXI4BundleAxiBundle(axi4Bundle, axiBundle)
  }

  def connectAxiBundleAXI4Bundle(
      axiBundle: LmAxiBundle,
      axi4Bundle: AXI4Bundle
  ) = {
    // ar channel
    axiBundle.ar.bits.addr := axi4Bundle.ar.bits.addr
    axiBundle.ar.bits.id := axi4Bundle.ar.bits.id
    axiBundle.ar.bits.len := axi4Bundle.ar.bits.len
    axiBundle.ar.bits.size := axi4Bundle.ar.bits.size
    axiBundle.ar.bits.burst := axi4Bundle.ar.bits.burst
    axiBundle.ar.bits.lock := axi4Bundle.ar.bits.lock
    axiBundle.ar.bits.cache := axi4Bundle.ar.bits.cache
    axiBundle.ar.bits.prot := axi4Bundle.ar.bits.prot
    axiBundle.ar.bits.qos := axi4Bundle.ar.bits.qos
    axiBundle.ar.bits.region := DontCare
    axiBundle.ar.bits.user := DontCare
    axiBundle.ar.valid := axi4Bundle.ar.valid
    axi4Bundle.ar.ready := axiBundle.ar.ready

    // aw channel
    axiBundle.aw.bits.addr := axi4Bundle.aw.bits.addr
    axiBundle.aw.bits.id := axi4Bundle.aw.bits.id
    axiBundle.aw.bits.len := axi4Bundle.aw.bits.len
    axiBundle.aw.bits.size := axi4Bundle.aw.bits.size
    axiBundle.aw.bits.burst := axi4Bundle.aw.bits.burst
    axiBundle.aw.bits.lock := axi4Bundle.aw.bits.lock
    axiBundle.aw.bits.cache := axi4Bundle.aw.bits.cache
    axiBundle.aw.bits.prot := axi4Bundle.aw.bits.prot
    axiBundle.aw.bits.qos := axi4Bundle.aw.bits.qos
    axiBundle.aw.bits.region := DontCare
    axiBundle.aw.bits.user := DontCare
    axiBundle.aw.bits.atop := 0.U
    axiBundle.aw.valid := axi4Bundle.aw.valid
    axi4Bundle.aw.ready := axiBundle.aw.ready

    // w channel
    axiBundle.w.bits.data := axi4Bundle.w.bits.data
    axiBundle.w.bits.strb := axi4Bundle.w.bits.strb
    axiBundle.w.bits.last := axi4Bundle.w.bits.last
    axiBundle.w.bits.user := DontCare
    axiBundle.w.valid := axi4Bundle.w.valid
    axi4Bundle.w.ready := axiBundle.w.ready

    // b channel
    axi4Bundle.b.bits.id := axiBundle.b.bits.id
    axi4Bundle.b.bits.resp := axiBundle.b.bits.resp
    axi4Bundle.b.valid := axiBundle.b.valid
    axiBundle.b.ready := axi4Bundle.b.ready

    // r channel
    axi4Bundle.r.bits.id := axiBundle.r.bits.id
    axi4Bundle.r.bits.data := axiBundle.r.bits.data
    axi4Bundle.r.bits.last := axiBundle.r.bits.last
    axi4Bundle.r.bits.resp := axiBundle.r.bits.resp
    axi4Bundle.r.valid := axiBundle.r.valid
    axiBundle.r.ready := axi4Bundle.r.ready
  }

  def connectAxi(
      axiBundle: LmAxiBundle,
      axi4Bundle: AXI4Bundle
  ) = connectAxiBundleAXI4Bundle(axiBundle, axi4Bundle)

  def connectAxi(extAxiBundle: LmExtAxiBundle, axi4Bundle: AXI4Bundle) {
    val axiBundle = Wire(new LmAxiBundle(extAxiBundle.params))
    connectAxiBundleAXI4Bundle(axiBundle, axi4Bundle)
    extAxiBundle <> axiBundle
    extAxiBundle.awatop := 0.U
  }

  def connectAxi(
      lmAxi: LmAxiBundle,
      rvtngAXI: rvtng.bus.AXI4Bundle
  ) = {
    lmAxi.ar.bits := DontCare
    lmAxi.ar.bits.addr := rvtngAXI.ar.bits.addr
    lmAxi.ar.bits.id := rvtngAXI.ar.bits.id
    lmAxi.ar.bits.size := rvtngAXI.ar.bits.size
    lmAxi.ar.bits.len := rvtngAXI.ar.bits.len
    lmAxi.ar.bits.burst := rvtngAXI.ar.bits.burst
    lmAxi.ar.valid := rvtngAXI.ar.valid
    rvtngAXI.ar.ready := lmAxi.ar.ready

    lmAxi.aw.bits := DontCare
    lmAxi.aw.bits.addr := rvtngAXI.aw.bits.addr
    lmAxi.aw.bits.id := rvtngAXI.aw.bits.id
    lmAxi.aw.bits.size := rvtngAXI.aw.bits.size
    lmAxi.aw.bits.len := rvtngAXI.aw.bits.len
    lmAxi.aw.bits.burst := rvtngAXI.aw.bits.burst
    lmAxi.aw.valid := rvtngAXI.aw.valid
    rvtngAXI.aw.ready := lmAxi.aw.ready

    rvtngAXI.r.bits := DontCare
    rvtngAXI.r.bits.id := lmAxi.r.bits.id
    rvtngAXI.r.bits.resp := lmAxi.r.bits.resp
    rvtngAXI.r.bits.data := lmAxi.r.bits.data
    rvtngAXI.r.bits.last := lmAxi.r.bits.last
    rvtngAXI.r.valid := lmAxi.r.valid
    lmAxi.r.ready := rvtngAXI.r.ready

    lmAxi.w.bits := DontCare
    lmAxi.w.bits.strb := rvtngAXI.w.bits.strb
    lmAxi.w.bits.data := rvtngAXI.w.bits.data
    lmAxi.w.bits.last := rvtngAXI.w.bits.last
    lmAxi.w.valid := rvtngAXI.w.valid
    rvtngAXI.w.ready := lmAxi.w.ready

    rvtngAXI.b.bits := DontCare
    rvtngAXI.b.bits.id := lmAxi.b.bits.id
    rvtngAXI.b.bits.resp := lmAxi.b.bits.resp
    rvtngAXI.b.valid := lmAxi.b.valid
    lmAxi.b.ready := rvtngAXI.b.ready
  }

  def genXbarSlvp(names: Seq[String], axip: LmAxiParams): Seq[PortParams] =
    names.map { name =>
      PortParams(
        axip = axip,
        async = Some(AsyncQueueParams()),
        name = name
      )
    }

  def connectTl(
      rvtngTL: rvtng.bus.TLBundle,
      rocketTL: freechips.rocketchip.tilelink.TLBundle, 
  ) = {
    rvtngTL.a.valid := rocketTL.a.valid
    // rvtngTL.a.bits := rocketTL.a.bits.asTypeOf(rvtngTL.a.bits)
    PartialAssign(rvtngTL.a.bits, rocketTL.a.bits)
    rocketTL.a.ready := rvtngTL.a.ready

    rocketTL.b.valid := rvtngTL.b.valid
    // rocketTL.b.bits := rvtngTL.b.bits.asTypeOf(rocketTL.b.bits)
    PartialAssign(rocketTL.b.bits, rvtngTL.b.bits)
    rvtngTL.b.ready := rocketTL.b.ready

    rvtngTL.c.valid := rocketTL.c.valid
    // rvtngTL.c.bits := rocketTL.c.bits.asTypeOf(rvtngTL.c.bits)
    PartialAssign(rvtngTL.c.bits, rocketTL.c.bits)
    rocketTL.c.ready := rvtngTL.c.ready

    rocketTL.d.valid := rvtngTL.d.valid
    // rocketTL.d.bits := rvtngTL.d.bits.asTypeOf(rocketTL.d.bits)
    PartialAssign(rocketTL.d.bits, rvtngTL.d.bits)
    rvtngTL.d.ready := rocketTL.d.ready

    rocketTL.e.valid := rvtngTL.e.valid
    // rocketTL.e.bits := rvtngTL.e.bits.asTypeOf(rocketTL.e.bits)
    PartialAssign(rocketTL.e.bits, rvtngTL.e.bits)
    rvtngTL.e.ready := rocketTL.e.ready
  }

  def connectTl(
      rocketTL: freechips.rocketchip.tilelink.TLBundle,
      rvtngTL: rvtng.bus.TLBundle, 
  ) = {
    rocketTL.a.valid := rvtngTL.a.valid
    // rvtngTL.a.bits := rocketTL.a.bits.asTypeOf(rvtngTL.a.bits)
    PartialAssign(rocketTL.a.bits, rvtngTL.a.bits)
    rvtngTL.a.ready := rocketTL.a.ready

    rvtngTL.b.valid := rocketTL.b.valid
    // rocketTL.b.bits := rvtngTL.b.bits.asTypeOf(rocketTL.b.bits)
    PartialAssign(rvtngTL.b.bits, rocketTL.b.bits)
    rocketTL.b.ready := rvtngTL.b.ready

    rocketTL.c.valid := rvtngTL.c.valid
    // rvtngTL.c.bits := rocketTL.c.bits.asTypeOf(rvtngTL.c.bits)
    PartialAssign(rocketTL.c.bits, rvtngTL.c.bits)
    rvtngTL.c.ready := rocketTL.c.ready

    rvtngTL.d.valid := rocketTL.d.valid
    // rocketTL.d.bits := rvtngTL.d.bits.asTypeOf(rocketTL.d.bits)
    PartialAssign(rvtngTL.d.bits, rocketTL.d.bits)
    rocketTL.d.ready := rvtngTL.d.ready

    rvtngTL.e.valid := rocketTL.e.valid
    // rocketTL.e.bits := rvtngTL.e.bits.asTypeOf(rocketTL.e.bits)
    PartialAssign(rvtngTL.e.bits, rocketTL.e.bits)
    rocketTL.e.ready := rvtngTL.e.ready
  }
}
