package rvtsoc.subsystem

import chisel3._
import chisel3.util._
import chisel3.experimental.hierarchy.{
  instantiable,
  public,
  Definition,
  Instance
}

import org.chipsalliance.cde.config
import org.chipsalliance.cde.config.Parameters
import org.chipsalliance.diplomacy.lazymodule._

import lmss.axi.{ExtAxiBundle, AxiBundle => LmAxiBundle}
import rvtsoc.subsystem.XbarParamsKey

import freechips.rocketchip.devices.tilelink.BootROMLocated
import freechips.rocketchip.subsystem._
import freechips.rocketchip.util.StringToAugmentedString
import freechips.rocketchip.resources.{MemoryDevice, SimpleDevice}
import freechips.rocketchip.diplomacy.{
  LazyModule,
  BufferParams,
  IdRange,
  RegionType,
  TransferSizes
}
import freechips.rocketchip.devices.tilelink._
import freechips.rocketchip.tile.MaxHartIdBits

import rvtsoc.config.WiredrvParamsKey

import rvtng.top.{RVTNGTop, TensorInst, TensorResp}
import rvtsoc.config.RvtngParamsKey
import freechips.rocketchip.amba.axi4.AXI4MasterNode
import freechips.rocketchip.amba.axi4.AXI4MasterParameters
import freechips.rocketchip.amba.axi4.AXI4MasterPortParameters
import freechips.rocketchip.diplomacy.IdRange

import freechips.rocketchip.tilelink._
import freechips.rocketchip.amba.axi4.AXI4Fragmenter
import freechips.rocketchip.amba.axi4.AXI4IdIndexer
import freechips.rocketchip.amba.axi4.AXI4ToTL
import freechips.rocketchip.amba.axi4.AXI4UserYanker
import freechips.rocketchip.diplomacy.BufferParams
import freechips.rocketchip.tilelink.TLBuffer
import freechips.rocketchip.tilelink.TLFIFOFixer
import freechips.rocketchip.tilelink.TLWidthWidget

import rvtsoc.blackbox

class BaseScalarCore(implicit p: config.Parameters) extends Module {
  val maxHartIdBits = p(MaxHartIdBits)
  val scalarAxip = p(XbarParamsKey).scalarAxip
  val bootromP = p(BootROMLocated(InSubsystem))
  require(bootromP.size == 1)

  val io = IO(new Bundle {
    val hartId = Input(UInt(maxHartIdBits.W))
    val m_axi = new ExtAxiBundle(scalarAxip)
    val mtip = Input(Bool())
    val msip = Input(Bool())
    val meip = Input(Bool())
    val debug = Input(Bool())
    val time = Input(ValidIO(UInt(64.W)))
    val tensorInst = Decoupled(new TensorInst)
    val tensorResp = Flipped(Decoupled(new TensorResp))
  })
}

class BaseRvtngCore(implicit p: config.Parameters) extends Module {
  val maxHartIdBits = p(MaxHartIdBits)

  val tensorAxip = p(XbarParamsKey).tensorAxip
  val uramAxip = p(XbarParamsKey).uramAxip
  val uramAddrSet = p(XbarParamsKey).uramAddrSet
  val uramErrAddrSet = p(XbarParamsKey).uramErrAddrSet

  val io = IO(new Bundle {
    val hartId = Input(UInt(maxHartIdBits.W))
    val instReq = Flipped(Decoupled(new TensorInst))
    val instResp = Decoupled(new TensorResp)
    val m_axi = new LmAxiBundle(tensorAxip)
    val s_axi = Flipped(new LmAxiBundle(uramAxip))
  })
}

class RvtngCore(implicit p: config.Parameters) extends BaseRvtngCore {

  val matParams = p(RvtngParamsKey).get.matParams
  val vecParams = p(RvtngParamsKey).get.vecParams
  val dmaParams = p(RvtngParamsKey).get.dmaParams
  val vpuParams = p(RvtngParamsKey).get.vpuParams
  val tlParms = p(RvtngParamsKey).get.tlParms

  val rvtng = Module(
    new RVTNGTop(matParams, vecParams, dmaParams, vpuParams, tlParms)
  )

  class Axi2Tl(implicit p: Parameters) extends LazyModule {
    private val axiPortName = "uram_axi4"
    val axiNode =
      AXI4MasterNode(
        Seq(
          AXI4MasterPortParameters(
            masters = Seq(
              AXI4MasterParameters(
                name = axiPortName.kebab,
                id = IdRange(0, 1 << tlParms.sourceBits),
                maxFlight = Some(1)
              )
            )
          )
        )
      )
    val uramDevice = new SimpleDevice("uram", Seq("rvtng,uram0"))
    private val uramBeatBytes = tlParms.dataBits / 8
    val uramNode = TLManagerNode(
      Seq(
        TLSlavePortParameters.v1(
          Seq(
            TLSlaveParameters.v1(
              address = List(uramAddrSet),
              resources = uramDevice.reg("mem"),
              regionType = RegionType.IDEMPOTENT,
              supportsGet = TransferSizes(
                1,
                uramBeatBytes
              ),
              supportsPutPartial = TransferSizes(1, uramBeatBytes),
              supportsPutFull = TransferSizes(1, uramBeatBytes),
              fifoId = Some(0)
            )
          ), // requests are handled in order
          beatBytes = uramBeatBytes
        )
      )
    )
    val error = LazyModule(
      new TLError(
        DevNullParams(
          List(uramErrAddrSet),
          maxAtomic = 8,
          maxTransfer = 4096
        ),
        beatBytes = uramBeatBytes
      )
    )
    val xbar = LazyModule(new TLXbar(nameSuffix = Some(s"UramTlXbar")))

    uramNode := xbar.node
    error.node := xbar.node

    (xbar.node
      := TLBuffer(BufferParams.default)
      := TLFIFOFixer(TLFIFOFixer.all)
      := AXI4ToTL()
      := AXI4UserYanker(Some(1))
      := AXI4Fragmenter()
      := AXI4IdIndexer(uramAxip.idBits)
      := axiNode)

    lazy val module = new Axi2TlImpl
    class Axi2TlImpl extends LazyModuleImp(this) {
      val io = IO(new Bundle {
        val axi = Flipped(new LmAxiBundle(uramAxip))
        val tl = new TLBundle(
          TLBundleParameters.emptyBundleParams.copy(
            addressBits = tlParms.addressBits,
            dataBits = tlParms.dataBits,
            sourceBits = tlParms.sourceBits,
            sinkBits = tlParms.sinkBits,
            hasBCE = tlParms.hasBCE
          )
        )
      })
      utils.connectAxi(axiNode.out.head._1, io.axi)
      io.tl <> uramNode.in.head._1
      require(tlParms.dataBits == uramNode.in.head._1.params.dataBits)
      require(
        tlParms.sourceBits >= uramNode.in.head._1.params.sourceBits,
        s"require tlParms.sourceBits >= uramNode.in.head._1.params.sourceBits + 1, but ${tlParms.sourceBits} < ${uramNode.in.head._1.params.sourceBits}"
      )
    }
  }

  val laxi2Tl = LazyModule(new Axi2Tl)
  val axi2Tl = Module(laxi2Tl.module)

  utils.connectTl(rvtng.tl, axi2Tl.io.tl)

  rvtng.instReq <> io.instReq
  io.instResp <> rvtng.instResp
  utils.connectAxi(io.m_axi, rvtng.axi4)
  axi2Tl.io.axi <> io.s_axi
}

@instantiable
class BaseNpuCore(implicit p: Parameters) extends Module {
  val scalarAxip = p(XbarParamsKey).scalarAxip
  val tensorAxip = p(XbarParamsKey).tensorAxip
  val uramAxip = p(XbarParamsKey).uramAxip
  val maxHartIdBits = p(MaxHartIdBits)

  @public val io = IO(new Bundle {
    val hartId = Input(UInt(maxHartIdBits.W))
    val s_axi_tensor = Flipped(new ExtAxiBundle(uramAxip))
    val m_axi_tensor = new ExtAxiBundle(tensorAxip)
    val m_axi_scalar = new ExtAxiBundle(scalarAxip)
    val scalar_int_in = Input(
      new Bundle {
        val mtip = Bool()
        val msip = Bool()
        val meip = Bool()
        val debug = Bool()
      }
    )
    val tensor_int_out = Output(new Bool())
    val time = Input(ValidIO(UInt(64.W)))
  })
}

class NpuCore(implicit p: Parameters) extends BaseNpuCore {

  val scalarCore = Module(
    if (p(WiredrvParamsKey).isDefined) (new WiredrvCore)
    else (new blackbox.ScalarCore)
  )
  val tensorCore = Module(
    if (p(RvtngParamsKey).isDefined) (new RvtngCore)
    else (new blackbox.RvtngCore)
  )

  scalarCore.io.hartId := io.hartId
  io.m_axi_scalar <> scalarCore.io.m_axi
  scalarCore.io.mtip := io.scalar_int_in.mtip
  scalarCore.io.msip := io.scalar_int_in.msip
  scalarCore.io.meip := io.scalar_int_in.meip
  scalarCore.io.debug := io.scalar_int_in.debug
  scalarCore.io.time := io.time

  tensorCore.io.hartId := io.hartId
  tensorCore.io.instReq <> scalarCore.io.tensorInst
  scalarCore.io.tensorResp <> tensorCore.io.instResp

  tensorCore.io.s_axi <> io.s_axi_tensor
  io.m_axi_tensor <> tensorCore.io.m_axi
  io.tensor_int_out := DontCare

}
