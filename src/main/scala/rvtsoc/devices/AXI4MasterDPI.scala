/*
 * Copyright (c) 2026 Institute of Computing Technology, Chinese Academy of Sciences
 * SPDX-License-Identifier: MulanPSL-2.0
 *
 * Dayan is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *     http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

package rvtsoc.devices

import chisel3._
import chisel3.util._

import freechips.rocketchip.amba.axi4._

import rvtsoc.utils.ExtraFiles

class AXI4MasterDPIA(params: AXI4BundleParameters, name: String)
    extends HasExtModuleInline
    with HasExtModuleResource {
  val clock = IO(Input(Clock()))
  val reset = IO(Input(Reset()))
  val valid = IO(Output(Bool()))
  val ready = IO(Input(Bool()))
  val id = IO(Output(UInt(params.idBits.W)))
  val addr = IO(Output(UInt(params.addrBits.W)))
  val len = IO(Output(UInt(params.lenBits.W)))
  val size = IO(Output(UInt(params.sizeBits.W)))
  val burst = IO(Output(UInt(params.burstBits.W)))

  val funcName =
    s"${name}_${params.dataBits}_tick"
  setInline(
    s"${name}.sv",
    s"""
      |module ${name} (
      |  input  logic clock,
      |  input  logic reset,
      |  output logic valid,
      |  input  logic ready,
      |  output logic [${params.idBits - 1}:0]    id,
      |  output logic [${params.addrBits - 1}:0]  addr,
      |  output logic [${params.lenBits - 1}:0]   len,
      |  output logic [${params.sizeBits - 1}:0]  size,
      |  output logic [${params.burstBits - 1}:0] burst
      |);
      |  /* verilator lint_off WIDTH */
      |  import "DPI-C" function void ${funcName}(
      |    output bit valid,
      |    input  bit ready,
      |    output int id,
      |    output longint addr,
      |    output int len,
      |    output int size,
      |    output int burst
      |  );
      |  logic _valid;
      |  logic [${params.idBits - 1}:0]    _id;
      |  logic [${params.addrBits - 1}:0]  _addr;
      |  logic [${params.lenBits - 1}:0]   _len;
      |  logic [${params.sizeBits - 1}:0]  _size;
      |  logic [${params.burstBits - 1}:0] _burst;
      |  always_ff @(posedge clock) begin
      |    if (reset) begin
      |      valid <= 0;
      |      id    <= 0;
      |      addr  <= 0;
      |      len   <= 0;
      |      size  <= 0;
      |      burst <= 0;
      |    end else begin
      |      ${funcName}(_valid, ready, _id, _addr, _len, _size, _burst);
      |      valid <= _valid;
      |      id    <= _id;
      |      addr  <= _addr;
      |      len   <= _len;
      |      size  <= _size;
      |      burst <= _burst;
      |    end
      |  end
      |
      |endmodule
      |""".stripMargin
  )
}

class AXI4MasterDPIAW(params: AXI4BundleParameters)
    extends AXI4MasterDPIA(params, "AXI4MasterDPIAW")

class AXI4MasterDPIW(params: AXI4BundleParameters) extends HasExtModuleInline {
  val clock = IO(Input(Clock()))
  val reset = IO(Input(Reset()))
  val valid = IO(Output(Bool()))
  val ready = IO(Input(Bool()))
  val data = IO(Output(UInt(params.dataBits.W)))
  val strb = IO(Output(UInt((params.dataBits / 8).W)))
  val last = IO(Output(Bool()))

  val funcName =
    s"AXI4MasterDPIW_${params.dataBits}_tick"
  setInline(
    s"AXI4MasterDPIW.sv",
    s"""
      |module AXI4MasterDPIW (
      |  input  logic clock,
      |  input  logic reset,
      |  output logic valid,
      |  input  logic ready,
      |  output logic [${params.dataBits - 1}:0] data,
      |  output logic [${(params.dataBits / 8) - 1}:0] strb,
      |  output logic last
      |);
      |  /* verilator lint_off WIDTH */
      |  import "DPI-C" function void ${funcName}(
      |    output bit valid,
      |    input  bit ready,
      |    output byte data[${params.dataBits / 8 - 1}:0],
      |    output byte strb[${params.dataBits / 8 / 8 - 1}:0],
      |    output bit last
      |  );
      |  logic _valid;
      |  byte  _data [${params.dataBits / 8 - 1}:0];
      |  byte  _strb [${params.dataBits / 8 / 8 - 1}:0];
      |  logic _last;
      |  always_ff @(posedge clock) begin
      |    if (reset) begin
      |      valid <= 0;
      |      data  <= 0;
      |      strb  <= 0;
      |      last  <= 0;
      |    end else begin
      |      ${funcName}(_valid, ready, _data, _strb, _last);
      |      valid <= _valid;
      |      for (int i = 0; i < ${params.dataBits / 8}; i = i + 1) begin : DATA_BYTES
      |        data[i*8 +: 8] <= _data[i];
      |      end
      |      for (int i = 0; i < ${params.dataBits / 8 / 8}; i = i + 1) begin : STRB_BYTES
      |        strb[i*8 +: 8] <= _strb[i];
      |      end
      |      last  <= _last;
      |    end
      |  end
      |
      |endmodule
      |""".stripMargin
  )
}

class AXI4MasterDPIB(params: AXI4BundleParameters) extends HasExtModuleInline {
  val clock = IO(Input(Clock()))
  val reset = IO(Input(Reset()))
  val valid = IO(Input(Bool()))
  val ready = IO(Output(Bool()))
  val id = IO(Input(UInt(params.idBits.W)))
  val resp = IO(Input(UInt(params.respBits.W)))

  val funcName = s"AXI4MasterDPIB_${params.dataBits}_tick"
  setInline(
    s"AXI4MasterDPIB.sv",
    s"""
      |module AXI4MasterDPIB (
      |  input  logic clock,
      |  input  logic reset,
      |  input  logic valid,
      |  output logic ready,
      |  input  logic [${params.idBits - 1}:0]   id,
      |  input  logic [${params.respBits - 1}:0] resp
      |);
      |  /* verilator lint_off WIDTH */
      |  import "DPI-C" function void ${funcName}(
      |    input  bit valid,
      |    output bit ready,
      |    input  int id,
      |    input  int resp
      |  );
      |  logic _ready;
      |  always_ff @(posedge clock) begin
      |    if (reset) begin
      |      ready <= 0;
      |    end else begin
      |      ${funcName}(valid, _ready, id, resp);
      |      ready <= _ready;
      |    end
      |  end
      |
      |endmodule
      |""".stripMargin
  )
}

class AXI4MasterDPIAR(params: AXI4BundleParameters)
    extends AXI4MasterDPIA(params, "AXI4MasterDPIAR")

class AXI4MasterDPIR(params: AXI4BundleParameters) extends HasExtModuleInline {
  val clock = IO(Input(Clock()))
  val reset = IO(Input(Reset()))
  val valid = IO(Input(Bool()))
  val ready = IO(Output(Bool()))
  val id = IO(Input(UInt(params.idBits.W)))
  val data = IO(Input(UInt(params.dataBits.W)))
  val resp = IO(Input(UInt(params.respBits.W)))
  val last = IO(Input(Bool()))

  val funcName =
    s"AXI4MasterDPIR_${params.dataBits}_tick"
  setInline(
    s"AXI4MasterDPIR.sv",
    s"""
      |module AXI4MasterDPIR (
      |  input  logic clock,
      |  input  logic reset,
      |  input  logic valid,
      |  output logic ready,
      |  input  logic [${params.idBits - 1}:0]   id,
      |  input  logic [${params.dataBits - 1}:0] data,
      |  input  logic [${params.respBits - 1}:0] resp,
      |  input  logic last
      |);
      |  /* verilator lint_off WIDTH */
      |  import "DPI-C" function void ${funcName}(
      |    input  bit valid,
      |    output bit ready,
      |    input  int id,
      |    input  byte data[${params.dataBits / 8 - 1}:0],
      |    input  int resp,
      |    input  bit last
      |  );
      |  logic _ready;
      |  byte  _data [${params.dataBits / 8 - 1}:0];
      |  always_ff @(posedge clock) begin
      |    if (reset) begin
      |      ready <= 0;
      |    end else begin
      |      ${funcName}(valid, _ready, id, _data, resp, last);
      |      ready <= _ready;
      |    end
      |  end
      |  generate
      |   genvar i;
      |   for (i = 0; i < ${params.dataBits / 8}; i = i + 1) begin : DATA_BYTES
      |     assign _data[i] = data[i*8 +: 8];
      |   end
      |  endgenerate
      |endmodule
      |""".stripMargin
  )
}

class AXI4MasterDPI(params: AXI4BundleParameters) extends Module {
  require(params.idBits <= 32)
  require(params.addrBits <= 64)
  require(params.lenBits <= 32)
  require(params.sizeBits <= 32)
  require(params.burstBits <= 32)
  require(params.dataBits / 8 <= 64)

  val axi = IO(new AXI4Bundle(params))

  val aw = Module(new AXI4MasterDPIAW(params))
  aw.clock := clock
  aw.reset := reset
  axi.aw.valid := aw.valid
  aw.ready := axi.aw.ready
  axi.aw.bits.id := aw.id
  axi.aw.bits.addr := aw.addr
  axi.aw.bits.len := aw.len
  axi.aw.bits.size := aw.size
  axi.aw.bits.burst := aw.burst
  axi.aw.bits.lock := 0.U
  axi.aw.bits.cache := 0.U
  axi.aw.bits.prot := 0.U
  axi.aw.bits.qos := 0.U

  val w = Module(new AXI4MasterDPIW(params))
  w.clock := clock
  w.reset := reset
  axi.w.valid := w.valid
  w.ready := axi.w.ready
  axi.w.bits.data := w.data
  axi.w.bits.strb := w.strb
  axi.w.bits.last := w.last

  val b = Module(new AXI4MasterDPIB(params))
  b.clock := clock
  b.reset := reset
  b.valid := axi.b.valid
  axi.b.ready := b.ready
  b.id := axi.b.bits.id
  b.resp := axi.b.bits.resp

  val ar = Module(new AXI4MasterDPIAR(params))
  ar.clock := clock
  ar.reset := reset
  axi.ar.valid := ar.valid
  ar.ready := axi.ar.ready
  axi.ar.bits.id := ar.id
  axi.ar.bits.addr := ar.addr
  axi.ar.bits.len := ar.len
  axi.ar.bits.size := ar.size
  axi.ar.bits.burst := ar.burst
  axi.ar.bits.lock := 0.U
  axi.ar.bits.cache := 0.U
  axi.ar.bits.prot := 0.U
  axi.ar.bits.qos := 0.U

  val r = Module(new AXI4MasterDPIR(params))
  r.clock := clock
  r.reset := reset
  r.valid := axi.r.valid
  axi.r.ready := r.ready
  r.id := axi.r.bits.id
  r.data := axi.r.bits.data
  r.resp := axi.r.bits.resp
  r.last := axi.r.bits.last

  ExtraFiles.add("src/main/csrc/devices/axi4master.cpp")
  ExtraFiles.addIncludePath("src/main/csrc")
}
