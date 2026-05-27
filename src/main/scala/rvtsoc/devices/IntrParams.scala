package rvtsoc.devices

import org.chipsalliance.cde.config._

case object IntrParamsKey extends Field[IntrParams]

case class IntrParams(
    nInterrupts: Int = 32,
    intrGroupSize: Int = 32
)
