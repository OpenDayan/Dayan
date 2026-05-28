package rvtsoc.devices

import chisel3._
import chisel3.util._

import freechips.rocketchip.amba.axi4._
import chisel3.SpecifiedDirection.Flip

import rvtsoc.utils.ExtraFiles

class AXI4SlaveDPIAWW(params: AXI4BundleParameters) extends HasExtModuleInline {
  val clock = IO(Input(Clock()))
  val reset = IO(Input(Reset()))
  val aw = IO(new Bundle {
    val valid = Input(Bool())
    val ready = Output(Bool())
    val id = Input(UInt(params.idBits.W))
    val addr = Input(UInt(params.addrBits.W))
    val len = Input(UInt(params.lenBits.W))
    val size = Input(UInt(params.sizeBits.W))
    val burst = Input(UInt(params.burstBits.W))
  })
  val w = IO(new Bundle {
    val valid = Input(Bool())
    val ready = Output(Bool())
    val data = Input(UInt(params.dataBits.W))
    val strb = Input(UInt((params.dataBits / 8).W))
    val last = Input(Bool())
  })

  val awFuncName =
    s"AXI4SlaveDPIAW_${params.dataBits}_tick"
  val wFuncName =
    s"AXI4SlaveDPIW_${params.dataBits}_tick"

  setInline(
    s"AXI4SlaveDPIAWW.sv",
    s"""
      |module AXI4SlaveDPIAWW (
      |  input  logic clock,
      |  input  logic reset,
      |  input  logic aw_valid,
      |  output logic aw_ready,
      |  input  logic [${params.idBits - 1}:0]    aw_id,
      |  input  logic [${params.addrBits - 1}:0]  aw_addr,
      |  input  logic [${params.lenBits - 1}:0]   aw_len,
      |  input  logic [${params.sizeBits - 1}:0]  aw_size,
      |  input  logic [${params.burstBits - 1}:0] aw_burst,
      |  input  logic w_valid,
      |  output logic w_ready,
      |  input  logic [${params.dataBits - 1}:0] w_data,
      |  input  logic [${(params.dataBits / 8) - 1}:0] w_strb,
      |  input  logic w_last
      |);
      |  /* verilator lint_off WIDTH */
      |  import "DPI-C" function void ${awFuncName}(
      |    input  bit valid,
      |    output bit ready,
      |    input  int id,
      |    input  longint addr,
      |    input  int len,
      |    input  int size,
      |    input  int burst
      |  );
      |  import "DPI-C" function void ${wFuncName}(
      |    input  bit valid,
      |    output bit ready,
      |    input  byte data[${params.dataBits / 8 - 1}:0],
      |    input  byte strb[${params.dataBits / 8 / 8 - 1}:0],
      |    input  bit last
      |  );
      |  logic _aw_ready;
      |  logic _w_ready;
      |  byte  _w_data [${params.dataBits / 8 - 1}:0];
      |  byte  _w_strb [${params.dataBits / 8 / 8 - 1}:0];
      |  always_ff @(posedge clock) begin
      |    if (reset) begin
      |      aw_ready <= 0;
      |      w_ready <= 0;
      |    end else begin
      |      ${awFuncName}(aw_valid, _aw_ready, aw_id, aw_addr, aw_len, aw_size, aw_burst);
      |      ${wFuncName}(w_valid, _w_ready, _w_data, _w_strb, w_last);
      |      aw_ready <= _aw_ready;
      |      w_ready <= _w_ready;
      |    end
      |  end
      |  generate
      |    genvar i;
      |    for (i = 0; i < ${params.dataBits / 8}; i = i + 1) begin : DATA_BYTES
      |      assign _w_data[i] = w_data[i*8 +: 8];
      |    end
      |    for (i = 0; i < ${params.dataBits / 8 / 8}; i = i + 1) begin : STRB_BYTES
      |      assign _w_strb[i] = w_strb[i*8 +: 8];
      |    end
      |  endgenerate
      |endmodule
      |""".stripMargin
  )
}

class AXI4SlaveDPIB(params: AXI4BundleParameters) extends HasExtModuleInline {
  val clock = IO(Input(Clock()))
  val reset = IO(Input(Reset()))
  val valid = IO(Output(Bool()))
  val ready = IO(Input(Bool()))
  val id = IO(Output(UInt(params.idBits.W)))
  val resp = IO(Output(UInt(2.W)))

  val funcName = s"AXI4SlaveDPIB_${params.dataBits}_tick"
  setInline(
    s"AXI4SlaveDPIB.sv",
    s"""
      |module AXI4SlaveDPIB (
      |  input  logic clock,
      |  input  logic reset,
      |  output logic valid,
      |  input  logic ready,
      |  output logic [${params.idBits - 1}:0]   id,
      |  output logic [1:0]                      resp
      |);
      |  /* verilator lint_off WIDTH */
      |  import "DPI-C" function void ${funcName}(
      |    output bit valid,
      |    input  bit ready,
      |    output int id,
      |    output int resp
      |  );
      |  logic _valid;
      |  logic [${params.idBits - 1}:0]   _id;
      |  logic [1:0]                      _resp;
      |  always_ff @(posedge clock) begin
      |    if (reset) begin
      |      valid <= 0;
      |      id    <= 0;
      |      resp  <= 0;
      |    end else begin
      |      ${funcName}(_valid, ready, _id, _resp);
      |      valid <= _valid;
      |      id    <= _id;
      |      resp  <= _resp;
      |    end
      |  end
      |endmodule
      |""".stripMargin
  )
}

class AXI4SlaveDPIAR(params: AXI4BundleParameters) extends HasExtModuleInline {
  val clock = IO(Input(Clock()))
  val reset = IO(Input(Reset()))
  val valid = IO(Input(Bool()))
  val ready = IO(Output(Bool()))
  val id = IO(Input(UInt(params.idBits.W)))
  val addr = IO(Input(UInt(params.addrBits.W)))
  val len = IO(Input(UInt(params.lenBits.W)))
  val size = IO(Input(UInt(params.sizeBits.W)))
  val burst = IO(Input(UInt(params.burstBits.W)))

  val funcName =
    s"AXI4SlaveDPIAR_${params.dataBits}_tick"
  setInline(
    s"AXI4SlaveDPIAR.sv",
    s"""
      |module AXI4SlaveDPIAR (
      |  input  logic clock,
      |  input  logic reset,
      |  input  logic valid,
      |  output logic ready,
      |  input  logic [${params.idBits - 1}:0]    id,
      |  input  logic [${params.addrBits - 1}:0]  addr,
      |  input  logic [${params.lenBits - 1}:0]   len,
      |  input  logic [${params.sizeBits - 1}:0]  size,
      |  input  logic [${params.burstBits - 1}:0] burst
      |);
      |  /* verilator lint_off WIDTH */
      |  import "DPI-C" function void ${funcName}(
      |    input  bit valid,
      |    output bit ready,
      |    input  int id,
      |    input  longint addr,
      |    input  int len,
      |    input  int size,
      |    input  int burst
      |  );
      |  logic _ready;
      |  always_ff @(posedge clock) begin
      |    if (reset) begin
      |      ready <= 0;
      |    end else begin
      |      ${funcName}(valid, _ready, id, addr, len, size, burst);
      |      ready <= _ready;
      |    end
      |  end
      |endmodule
      |""".stripMargin
  )
}

class AXI4SlaveDPIR(params: AXI4BundleParameters) extends HasExtModuleInline {
  val clock = IO(Input(Clock()))
  val reset = IO(Input(Reset()))
  val valid = IO(Output(Bool()))
  val ready = IO(Input(Bool()))
  val id = IO(Output(UInt(params.idBits.W)))
  val data = IO(Output(UInt(params.dataBits.W)))
  val resp = IO(Output(UInt(2.W)))
  val last = IO(Output(Bool()))

  val funcName =
    s"AXI4SlaveDPIR_${params.dataBits}_tick"
  setInline(
    s"AXI4SlaveDPIR.sv",
    s"""
      |module AXI4SlaveDPIR (
      |  input  logic clock,
      |  input  logic reset,
      |  output logic valid,
      |  input  logic ready,
      |  output logic [${params.idBits - 1}:0]    id,
      |  output logic [${params.dataBits - 1}:0]  data,
      |  output logic [1:0]                       resp,
      |  output logic last
      |);
      |  /* verilator lint_off WIDTH */
      |  import "DPI-C" function void ${funcName}(
      |    output bit valid,
      |    input  bit ready,
      |    output int id,
      |    output byte data[${params.dataBits / 8 - 1}:0],
      |    output int resp,
      |    output bit last
      |  );
      |  logic _valid;
      |  logic [${params.idBits - 1}:0]    _id;
      |  byte _data [${params.dataBits / 8 - 1}:0];
      |  logic [1:0]                       _resp;
      |  logic _last;
      |  always_ff @(posedge clock) begin
      |    if (reset) begin
      |      valid <= 0;
      |      id    <= 0;
      |      data  <= 0;
      |      resp  <= 0;
      |      last  <= 0;
      |    end else begin
      |      ${funcName}(_valid, ready, _id, _data, _resp, _last);
      |      valid <= _valid;
      |      id    <= _id;
      |      for (int i = 0; i < ${params.dataBits / 8}; i = i + 1) begin : DATA_BYTES
      |        data[i*8 +: 8] <= _data[i];
      |      end
      |      resp  <= _resp;
      |      last  <= _last;
      |    end
      |  end
      |endmodule
      |""".stripMargin
  )
}

class AXI4SlaveDPI(params: AXI4BundleParameters) extends Module {
  require(params.idBits <= 32)
  require(params.addrBits <= 64)
  require(params.lenBits <= 32)
  require(params.sizeBits <= 32)
  require(params.burstBits <= 32)
  require(params.dataBits / 8 <= 64)

  val axi = IO(Flipped(new AXI4Bundle(params)))

  val aww = Module(new AXI4SlaveDPIAWW(params))
  aww.clock := clock
  aww.reset := reset
  aww.aw.valid := axi.aw.valid
  axi.aw.ready := aww.aw.ready
  aww.aw.id := axi.aw.bits.id
  aww.aw.addr := axi.aw.bits.addr
  aww.aw.len := axi.aw.bits.len
  aww.aw.size := axi.aw.bits.size
  aww.aw.burst := axi.aw.bits.burst
  aww.w.valid := axi.w.valid
  axi.w.ready := aww.w.ready
  aww.w.data := axi.w.bits.data
  aww.w.strb := axi.w.bits.strb
  aww.w.last := axi.w.bits.last

  val b = Module(new AXI4SlaveDPIB(params))
  b.clock := clock
  b.reset := reset
  axi.b.valid := b.valid
  b.ready := axi.b.ready
  axi.b.bits.id := b.id
  axi.b.bits.resp := b.resp

  val ar = Module(new AXI4SlaveDPIAR(params))
  ar.clock := clock
  ar.reset := reset
  ar.valid := axi.ar.valid
  axi.ar.ready := ar.ready
  ar.id := axi.ar.bits.id
  ar.addr := axi.ar.bits.addr
  ar.len := axi.ar.bits.len
  ar.size := axi.ar.bits.size
  ar.burst := axi.ar.bits.burst

  val r = Module(new AXI4SlaveDPIR(params))
  r.clock := clock
  r.reset := reset
  axi.r.valid := r.valid
  r.ready := axi.r.ready
  axi.r.bits.id := r.id
  axi.r.bits.data := r.data
  axi.r.bits.resp := r.resp
  axi.r.bits.last := r.last

  ExtraFiles.add("src/main/csrc/devices/ddr4.cpp")
  ExtraFiles.addIncludePath("src/main/csrc")
}

import rvtng.bus.DPIAXI4
import rvtng.bus.{AXI4BundleParameters => RVTNGAXI4BundleParameters}
import rvtng.utils.PartialAssign

class AXI4SlaveDPILegacy(params: AXI4BundleParameters) extends Module {
  val axi = IO(Flipped(new AXI4Bundle(params)))

  private val axi4Parms = RVTNGAXI4BundleParameters(
    addressBits = params.addrBits,
    dataBits = params.dataBits,
    idBits = params.idBits
  )
  private val m = Module(new DPIAXI4(axi4Parms, 0))

  m.axi.ar.valid := axi.ar.valid
  axi.ar.ready := m.axi.ar.ready
  PartialAssign(m.axi.ar.bits, axi.ar.bits)

  m.axi.aw.valid := axi.aw.valid
  axi.aw.ready := m.axi.aw.ready
  PartialAssign(m.axi.aw.bits, axi.aw.bits)

  m.axi.w.valid := axi.w.valid
  axi.w.ready := m.axi.w.ready
  PartialAssign(m.axi.w.bits, axi.w.bits)

  axi.r.valid := m.axi.r.valid
  m.axi.r.ready := axi.r.ready
  PartialAssign(axi.r.bits, m.axi.r.bits)

  axi.b.valid := m.axi.b.valid
  m.axi.b.ready := axi.b.ready
  PartialAssign(axi.b.bits, m.axi.b.bits)

  ExtraFiles.add("src/main/csrc/devices/DPIAXI4.cpp")
  ExtraFiles.addIncludePath("src/main/csrc")
}
