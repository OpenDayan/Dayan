package rvtsoc.subsystem

import scala.collection.immutable.SortedMap

import java.nio.ByteBuffer
import java.nio.file.{Files, Paths}

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config._
import org.chipsalliance.diplomacy.lazymodule._
import freechips.rocketchip.tile._
import freechips.rocketchip.util.{
  StringToAugmentedString,
  BooleanToAugmentedBoolean,
  SystemFileName,
  ResourceFileName,
  ElaborationArtefacts
}
import freechips.rocketchip.amba.axi4._
import freechips.rocketchip.amba.apb.{
  APBBundle,
  APBSlaveNode,
  APBSlavePortParameters,
  APBSlaveParameters
}
import freechips.rocketchip.tilelink._
import freechips.rocketchip.diplomacy.{
  LazyModule,
  BufferParams,
  IdRange,
  RegionType,
  TransferSizes,
  AddressSet,
  AddressRange
}
import freechips.rocketchip.resources.{
  MemoryDevice,
  SimpleDevice,
  ResourceBinding,
  BindingScope,
  AddressMapEntry,
  ResourceBindings
}
import freechips.rocketchip.interrupts._
import freechips.rocketchip.devices.tilelink._
import freechips.rocketchip.subsystem.{InSubsystem, HasDTS}
import freechips.rocketchip.devices.debug._
import freechips.rocketchip.jtag._

import xs.infra.axi.{AxiParams, ExtAxiBundle, AxiBundle}

import InterruptModule.InterruptModule

import rvtsoc.devices._

case object RtcPeriodKey extends Field[BigInt]

class ControlBlock(implicit p: Parameters) extends LazyModule with HasDTS {
  val controlAxip = p(XbarParamsKey).controlAxip
  val frontAxip = p(XbarParamsKey).frontAxip
  val nCores = 1
  val systemAddrSet = p(XbarParamsKey).systemAddrSet
  val errorAddrSet = p(XbarParamsKey).errorAddrSet
  val intrHandlerBase = p(XbarParamsKey).intrHandlerBase
  val nInterrupts = p(IntrParamsKey).nInterrupts
  val intrGroupSize = p(IntrParamsKey).intrGroupSize

  private val axiPortName = "control_axi4"
  val axiNode =
    AXI4MasterNode(
      Seq(
        AXI4MasterPortParameters(
          masters = Seq(
            AXI4MasterParameters(
              name = axiPortName.kebab,
              id = IdRange(0, 1 << controlAxip.idBits),
              maxFlight = Some(1)
            )
          )
        )
      )
    )

  val xbar = LazyModule(new TLXbar(nameSuffix = Some(s"ControlXbar")))
  (xbar.node
    := TLBuffer(BufferParams.default)
    := TLFIFOFixer(TLFIFOFixer.all)
    := TLWidthWidget(controlAxip.dataBits / 8)
    := AXI4ToTL()
    := AXI4UserYanker(Some(1))
    := AXI4Fragmenter()
    := AXI4IdIndexer(controlAxip.idBits)
    := axiNode)

  val clintNodes = (0 until nCores).map { i =>
    val clintNode = IntSinkNode(IntSinkPortSimple())
    (i, clintNode)
  }
  val plicNodes = p(PLICKey).map { params =>
    (0 until nCores).map { i =>
      val plicNode = IntSinkNode(IntSinkPortSimple())
      (i, plicNode)
    }
  }

  val clint = p(CLINTKey).map { params =>
    val clint = LazyModule(new CLINT(params, controlAxip.dataBits / 8))
    clint.node := xbar.node
    clintNodes.foreach { case (i, clintNode) =>
      clintNode := clint.intnode
    }
    clint
  }

  val device = new SimpleDevice("interrupt-tensor", Seq("rvtsoc,interrupt"))
  val tensorIntNode = p(PLICKey).map { params =>
    IntSourceNode(
      IntSourcePortSimple(num = nCores, resources = device.int)
    )
  }
  val plic = p(PLICKey).map { params =>
    val plic = LazyModule(new TLPLIC(params, controlAxip.dataBits / 8))
    plic.node := xbar.node
    plicNodes.get.foreach { case (i, plicNode) =>
      plicNode := plic.intnode
    }
    plic.intnode := tensorIntNode.get
    (plic, tensorIntNode)
  }

  val bootroms = p(BootROMLocated(InSubsystem)).map { params =>
    val contents = params.contentFileName match {
      case SystemFileName(fileName) => {
        val romdata = Files.readAllBytes(Paths.get(fileName))
        ByteBuffer.wrap(romdata).array()
      }
      case ResourceFileName(fileName) => {
        val file = os.resource / os.RelPath(fileName.dropWhile(_ == '/'))
        os.read.bytes(file)
      }
    }

    val bootrom = LazyModule(
      new TLROM(
        params.address,
        params.size,
        contents.toIndexedSeq,
        true,
        controlAxip.dataBits / 8
      )
    )
    bootrom.node := xbar.node
    bootrom
  }

  val bootAddrReg = p(BootAddrRegKey).map { params =>
    val reg = LazyModule(new BootAddrReg(params, controlAxip.dataBits / 8, nCores))
    reg.node := xbar.node
    reg
  }

  val error = LazyModule(
    new TLError(
      DevNullParams(
        List(errorAddrSet),
        maxAtomic = 8,
        maxTransfer = 4096
      ),
      beatBytes = controlAxip.dataBits / 8
    )
  )
  error.node := xbar.node

  val interruptModule = LazyModule(
    new InterruptModule(
      base = intrHandlerBase,
      numInterrupts = nInterrupts,
      groupSize = intrGroupSize
    )
  )
  interruptModule.node := xbar.node

  val debugCustomXbarOpt = p(DebugModuleKey).map(params =>
    LazyModule(new DebugCustomXbar(outputRequiresInput = false))
  )
  val debugOpt = p(DebugModuleKey).map { params =>
    val tlDM = LazyModule(new TLDebugModule(controlAxip.dataBits / 8))
    tlDM.node := xbar.node
    tlDM.dmInner.dmInner.customNode := debugCustomXbarOpt.get.node
    tlDM
  }
  val debugAxiNode = debugOpt.flatMap { debugmod =>
    debugmod.dmInner.dmInner.sb2tlOpt.map { sb2tl =>
      val debugAxiNode = AXI4SlaveNode(
        Seq(
          AXI4SlavePortParameters(
            Seq(
              AXI4SlaveParameters(
                Seq(systemAddrSet),
                regionType = RegionType.UNCACHED,
                supportsRead = TransferSizes(1, frontAxip.dataBits),
                supportsWrite = TransferSizes(1, frontAxip.dataBits),
                interleavedId = Some(0)
              )
            ),
            beatBytes = frontAxip.dataBits / 8
          )
        )
      )
      // TODO
      (debugAxiNode
        := AXI4UserYanker()
        := AXI4IdIndexer(frontAxip.idBits)
        := TLToAXI4()
        := TLWidthWidget(1)
        := sb2tl.node)
      debugAxiNode
    }
  }
  val debugIntrNodes = debugOpt.map { debugmod =>
    (0 until nCores).map { i =>
      val crossingSink = IntSyncAsyncCrossingSink()
      crossingSink :*= debugmod.intnode
      val node = IntSinkNode(IntSinkPortSimple())
      node := crossingSink
      node
    }
  }

  lazy val module = new ControlBlockImpl
  class ControlBlockImpl extends LazyModuleImp(this) {
    val io = IO(new Bundle {
      val s_axi = Flipped(new AxiBundle(controlAxip))
      val scalar_ints_out = Output(
        Vec(
          nCores,
          new Bundle {
            val mtip = Bool()
            val msip = Bool()
            val meip = Bool()
            val debug = Bool()
          }
        )
      )
      val mtime = clint.map { lmod =>
        lmod.module.io.time.cloneType
      }
      val tensor_ints_in = Input(Vec(nCores, Bool()))
      val usr_int = interruptModule.module.io.cloneType
      val jtag = p(ExportDebug).jtag.option(Flipped(new JTAGIO))
      val m_axi = debugAxiNode.map { _ =>
        new AxiBundle(frontAxip)
      }
    })

    utils.connectAxi(axiNode.out.head._1, io.s_axi)

    clint.map { lmod =>
      val rtcPeriod = p(RtcPeriodKey)
      val (_, int_rtc_tick) = Counter(true.B, rtcPeriod.toInt)
      lmod.module.io.rtcTick := int_rtc_tick
      io.mtime.get := lmod.module.io.time
    }

    io.scalar_ints_out.zipWithIndex.foreach { case (int, i) =>
      int.mtip := clintNodes(i)._2.in.head._1(1)
      int.msip := clintNodes(i)._2.in.head._1(0)
      int.meip := (plicNodes match {
        case Some(x) => x(i)._2.in.head._1(0)
        case _       => io.tensor_ints_in(i)
      })
    }
    tensorIntNode.map { node =>
      node.out.head._1 := io.tensor_ints_in
    }

    io.usr_int <> interruptModule.module.io

    debugAxiNode.map { node =>
      utils.connectAxi(io.m_axi.get, node.in.head._1)
    }

    debugOpt.map { debugmod =>
      val debug = Wire(new DebugIO)

      Debug.connectDebugClockAndReset(Some(debug), clock)
      require(debug.clockeddmi.isEmpty, "DMI is Not Implemented")
      debug.systemjtag.foreach { sj =>
        sj.jtag <> io.jtag.get
        sj.reset := reset.asAsyncReset
        sj.mfr_id := p(JtagDTMKey).idcodeManufId.U(11.W)
        sj.part_number := p(JtagDTMKey).idcodePartNum.U(16.W)
        sj.version := p(JtagDTMKey).idcodeVersion.U(4.W)
      }

      val resetctrl =
        Wire(new ResetCtrlIO(debugmod.dmOuter.dmOuter.intnode.edges.out.size))
      resetctrl.hartIsInReset.map { _ := reset }

      io.scalar_ints_out.zipWithIndex.foreach { case (int, i) =>
        int.debug := debugIntrNodes.get(i).in.head._1(0)
      }

      debugmod.module.io.tl_clock := clock
      debugmod.module.io.tl_reset := reset.asBool
      debugmod.module.io.hartIsInReset := resetctrl.hartIsInReset
      resetctrl.hartResetReq.foreach { rcio =>
        debugmod.module.io.hartResetReq.foreach { rcdm => rcio := rcdm }
      }

      require(
        !(debug.clockeddmi.isDefined && debug.systemjtag.isDefined),
        "You cannot have both DMI and JTAG interface in ControlBlock"
      )

      require(
        !(debug.clockeddmi.isDefined && debug.apb.isDefined),
        "You cannot have both DMI and APB interface in ControlBlock"
      )

      require(
        !(debug.systemjtag.isDefined && debug.apb.isDefined),
        "You cannot have both APB and JTAG interface in ControlBlock"
      )

      debug.clockeddmi.foreach { dbg =>
        debugmod.module.io.dmi.get <> dbg
      }

      debugmod.module.io.debug_reset := debug.reset
      debugmod.module.io.debug_clock := debug.clock

      debug.ndreset := debugmod.module.io.ctrl.ndreset
      debug.dmactive := debugmod.module.io.ctrl.dmactive
      debugmod.module.io.ctrl.dmactiveAck := debug.dmactiveAck
      debug.extTrigger.foreach { x =>
        debugmod.module.io.extTrigger.foreach { y => x <> y }
      }

      // TODO in inheriting traits: Set this to something meaningful, e.g. "component is in reset or powered down"
      debugmod.module.io.ctrl.debugUnavail.foreach { _ := false.B }

      debug.systemjtag.foreach { sj =>
        val dtm = Module(
          new DebugTransportModuleJTAG(
            p(DebugModuleKey).get.nDMIAddrSize,
            p(JtagDTMKey)
          )
        )
        dtm.io.jtag <> sj.jtag

        debug.disableDebug.foreach { x =>
          dtm.io.jtag.TMS := sj.jtag.TMS | x
        } // force TMS high when debug is disabled

        dtm.io.jtag_clock := sj.jtag.TCK
        dtm.io.jtag_reset := sj.reset
        dtm.io.jtag_mfr_id := sj.mfr_id
        dtm.io.jtag_part_number := sj.part_number
        dtm.io.jtag_version := sj.version
        dtm.rf_reset := sj.reset

        debugmod.module.io.dmi.get.dmi <> dtm.io.dmi
        debugmod.module.io.dmi.get.dmiClock := sj.jtag.TCK
        debugmod.module.io.dmi.get.dmiReset := sj.reset
      }

    }

    val mapping = xbar.node.edges.out
      .flatMap(_.manager.managers)
      .flatMap { m =>
        AddressRange.fromSets(m.address).map { r =>
          AddressMapEntry(
            r,
            m.toResource.permissions,
            m.resources.map(_.owner.describe(ResourceBindings()).name)
          )
        }
      }
      .sortBy(_.range)

    println("ControlBlock Address Map")
    mapping.foreach { entry =>
      println(entry.toString((controlAxip.addrBits - 1) / 4 + 1))
    }
    println("")

    ElaborationArtefacts.add(
      "control-memmap.json",
      s"""{"mapping":[${mapping.map(_.toJSON).mkString(",")}]}"""
    )
  }
}
