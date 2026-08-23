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

package dayan.rvtsoc

import scala.io.Source
import scala.sys.process._

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
      ) replSeqMem: String = "false",
      @arg(
        name = "dis-verif",
        doc = "disable verification files"
      ) disVerif: String = "false",
      @arg(
        name = "abs-path",
        doc = "absolute path for filelist"
      ) absPath: String = "false"
  ) = {
    val outputPath = os.pwd / "generated" / s"$top-$config"
    val filelistPath = outputPath / "filelist.f"
    val includelistPath = outputPath / "includelist.f"
    val infoPath = outputPath / "info"
    val configNamePath = infoPath / "config.txt"
    val versionPath = infoPath / "version.txt"
    val absPathBoolean = absPath.toBoolean

    os.remove.all(outputPath)
    val p: Parameters =
      Class.forName("dayan.rvtsoc.config." + config).newInstance.asInstanceOf[Config]
    ChiselStage.emitSystemVerilog(
      Class
        .forName("dayan.rvtsoc.top." + top)
        .getConstructor(classOf[Parameters])
        .newInstance(p) match {
        case m: RawModule   => m
        case lm: LazyModule => LazyModule(lm).module
      },
      firtoolOpts = Array(
        "--disable-annotation-unknown",
        "--strip-debug-info",
        "--preserve-aggregate=vec",
        "--emit-separate-always-blocks",
        "--split-verilog",
        "--preserve-values=named",
        s"-o=${outputPath.toString()}"
      ) ++
        (if (replSeqMem.toBoolean)
           Array(
             "--repl-seq-mem",
             s"--repl-seq-mem-file=${infoPath / "mem.conf"}"
           )
         else Array()) ++
        (if (disVerif.toBoolean)
           Array("-disable-layers=Verification")
         else Array())
    )
    os.makeDir(infoPath)
    freechips.rocketchip.util.ElaborationArtefacts.files.foreach {
      case (ext, contents) =>
        os.write.over(
          infoPath / s"${ext}",
          contents()
        )
    }

    // Generated XS Files
    FileRegisters.write(outputPath.toString(), top)

    os.makeDir(outputPath / "wrappers")
    os.write.over(
      filelistPath,
      (os
        .read(filelistPath)
        .linesIterator
        .filter(_.nonEmpty)
        .map { _name =>
          val name = os.RelPath(_name)
          if (
            name.toString().startsWith("GENERIC_RAM_") ||
            name.toString().startsWith("sram_array_") ||
            name.toString() == "EICG_wrapper.v"
          ) {
            os.move(outputPath / name, outputPath / "wrappers" / name)
            s"wrappers/$name"
          } else name.toString()
        }
        ++ utils.ExtraFiles.files
          .map(name => s"../../$name"))
        .map(name =>
          if (absPathBoolean) (outputPath / os.RelPath(name)).toString()
          else name
        )
        .mkString("\n")
    )
    os.write.over(
      includelistPath,
      s"${utils.ExtraFiles.includePaths
          .map(name => s"../../$name")
          .map(name =>
            if (absPathBoolean) (outputPath / os.RelPath(name)).toString()
            else name
          )
          .mkString("\n")}\n"
    )
    os.write.over(
      configNamePath,
      s"$config\n"
    )

    val gitStr = try {
      "git describe --tags --always --dirty --abbrev=40".!!.trim
    } catch {
      case _: Exception => "unknown"
    }
    os.write.over(versionPath, s"$gitStr\n")
  }

  def main(args: Array[String]): Unit = ParserForMethods(this).runOrExit(args)
}
