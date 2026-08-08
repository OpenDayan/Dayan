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

package dayan.rvtsoc.subsystem

import chisel3._
import chisel3.util._
import chisel3.experimental.noPrefix

import org.chipsalliance.cde.config.Parameters
import org.chipsalliance.diplomacy.lazymodule.LazyModule

import freechips.rocketchip.util._

import xs.infra.axi._

class AxiXbar(
    mstParams: Seq[PortParams],
    slvMatchers: Seq[UInt => Bool],
    memParams: Seq[PortParams]
) extends BaseAxiXbar(mstParams.map(_.axip), memParams) {
  override val slvMatchersSeq = slvMatchers
  require(slvMatchersSeq.size == memParams.size)
  val dataBits = mstParams.head.axip.dataBits
  mstParams.zipWithIndex.foreach { case (p, i) =>
    require(
      p.axip.dataBits == dataBits,
      s"require mstParams ${p.name}.dataBits == ${dataBits}"
    )
  }
  memParams.zipWithIndex.foreach { case (p, i) =>
    require(
      p.axip.dataBits == dataBits,
      s"require memParams ${p.name}.dataBits == ${dataBits}"
    )
  }

  initialize()

}

class FlexAxiXbar(
    mstParams: Seq[PortParams],
    slvMatchers: Seq[UInt => Bool],
    memParams: Seq[PortParams]
) extends Module {
  val xbar = Module(
    new AxiXbar(
      mstParams,
      slvMatchers,
      memParams.map { p =>
        p.copy(p.axip.copy(dataBits = mstParams.head.axip.dataBits))
      }
    )
  )

  val io = IO(new Bundle {
    val upstream =
      MixedVec(mstParams.map(params => Flipped(new AxiBundle(params.axip))))
    val downstream =
      MixedVec(memParams.map(params => new AxiBundle(params.axip)))
  })

  xbar.io.upstream <> io.upstream
  io.downstream.zip(xbar.io.downstream).zip(memParams).foreach {
    case ((ds, xds), p) =>
      require(
        ds.params.idBits >= xds.params.idBits,
        s"require ${p.name}.idBits >= ${xds.params.idBits}"
      )

      if (ds.params.dataBits < xds.params.dataBits) {
        val axiWtoN = Module(
          new AxiWideToNarrow(
            xds.params,
            ds.params,
            2
          )
        )
        axiWtoN.suggestName(s"axiWtoN_${p.name}")
        axiWtoN.io.mst <> xds
        ds <> axiWtoN.io.slv
      } else if (ds.params.dataBits > xds.params.dataBits) {
        val axiNtoW = Module(
          new AxiNarrowToWide(
            xds.params,
            ds.params,
            2
          )
        )
        axiNtoW.suggestName(s"axiNtoW_${p.name}")
        axiNtoW.io.mst <> xds
        ds <> axiNtoW.io.slv
      } else {
        ds <> xds
      }
  }

  def getPortId(axip: Seq[PortParams], name: String) =
    axip.indexWhere(_.name == name)
  def getMst(name: String) =
    io.downstream(getPortId(memParams, name))
  def getSlv(name: String) =
    io.upstream(getPortId(mstParams, name))

  def printAddrMap(name: String, aw: Int) = {
    println(s"$name Address Map")
    memParams.foreach { p =>
      println(
        s"\t%${aw}x - %${aw}x %s"
          .format(p.addr.base, p.addr.base + p.addr.size, p.name)
      )
    }
    println("")
  }

  printAddrMap(
    getClass().getName(),
    memParams.map(p => (p.axip.addrBits - 1) / 4 + 1).max
  )

}

class SystemXbar(slvPortParams: Seq[PortParams], mstPortParams: Seq[PortParams])
    extends FlexAxiXbar(
      slvPortParams,
      mstPortParams.map(_.addr.test),
      mstPortParams
    )

