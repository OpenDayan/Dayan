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

package rvtsoc

import scala.io.Source

import chisel3._
import _root_.circt.stage.ChiselStage

import mainargs._

import org.chipsalliance.cde.config.{Config, Parameters}
import freechips.rocketchip.diplomacy.LazyModule
import xs.utils._

object Main {
  @main def _main(
      @arg(
        name = "top",
        doc = "top Module or LazyModule"
      ) top: String = "TestRvtsoc",
      @arg(name = "config", doc = "CDE configs") config: String,
      @arg(
        name = "repl-seq-mem",
        doc = "replace sequential memory"
      ) replSeqMem: String = "false"
  ) = {
    val output_path = os.pwd / "generated" / top
    println()
    val filelist_path = output_path / "filelist.f"
    val includelist_path = output_path / "includelist.f"

    os.remove.all(output_path)
    val p: Parameters =
      Class.forName("rvtsoc.config." + config).newInstance.asInstanceOf[Config]
    ChiselStage.emitSystemVerilog(
      Class
        .forName("rvtsoc.top." + top)
        .getConstructor(classOf[Parameters])
        .newInstance(p) match {
        case m: RawModule   => m
        case lm: LazyModule => LazyModule(lm).module
      },
      firtoolOpts = Array(
        "--disable-annotation-unknown",
        "--strip-debug-info",
        "--preserve-aggregate=vec",
        "--split-verilog",
        "--preserve-values=named",
        s"-o=${output_path.toString()}"
      ) ++ (if (replSeqMem.toBoolean)
              Array("--repl-seq-mem", s"--repl-seq-mem-file=${top}.mem.conf")
            else Array())
    )
    freechips.rocketchip.util.ElaborationArtefacts.files.foreach {
      case (ext, contents) =>
        os.write.over(
          output_path / s"${top}.${ext}",
          contents()
        )
    }

    // Generated XS Files
    FileRegisters.write(output_path.toString(), top)

    os.write.over(
      filelist_path,
      os.read(filelist_path)
        .linesIterator
        .filter(_.nonEmpty)
        .map(output_path / os.RelPath(_))
        .mkString(
          "\n"
        ) + s"\n${rvtsoc.utils.ExtraFiles.files.mkString("\n")}\n"
    )
    os.write.over(
      includelist_path,
      s"${rvtsoc.utils.ExtraFiles.includePaths.mkString("\n")}\n"
    )
  }

  def main(args: Array[String]): Unit = ParserForMethods(this).runOrExit(args)
}
