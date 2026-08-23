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

package dayan.rvtsoc.utils

import scala.reflect.io.Path

object ExtraFiles {
  var files: Seq[String] = Nil
  var includePaths: Seq[String] = Nil

  def add(filename: String): Unit = {
    files = Path(filename).toString +: files
  }

  def addResource(filename: String): Unit = {
    val path = getClass.getResource(filename).getPath
    if (!files.contains(path)) files = path +: files
  }

  def addIncludePath(path: String): Unit = {
    includePaths = Path(path).toString +: includePaths
  }

  def addResourceIncludePath(path: String): Unit = {
    val resourcePath = getClass.getResource(path).getPath
    if (!includePaths.contains(resourcePath)) {
      includePaths = resourcePath +: includePaths
    }
  }

  def contains(filename: String): Boolean = {
    files.contains(Path(filename).toAbsolute.toString)
  }
}
