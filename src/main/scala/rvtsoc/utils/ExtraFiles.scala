package rvtsoc.utils

import scala.reflect.io.Path

object ExtraFiles {
  var files: Seq[String] = Nil
  var includePaths: Seq[String] = Nil

  def add(filename: String): Unit = {
    files = Path(filename).toAbsolute.toString +: files
  }

  def addResource(filename: String): Unit = {
    files = getClass.getResource(filename).getPath +: files
  }

  def addIncludePath(path: String): Unit = {
    includePaths = Path(path).toAbsolute.toString +: includePaths
  }

  def addResourceIncludePath(path: String): Unit = {
    includePaths = getClass.getResource(path).getPath +: includePaths
  }

  def contains(filename: String): Boolean = {
    files.foldLeft(false)((t, s) => { s == filename | t })
  }
}
