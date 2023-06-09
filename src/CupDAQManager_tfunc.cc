#include "TMessage.h"
#include "TMonitor.h"
#include "TRandom3.h"
#include "TServerSocket.h"

#include "OnlHistogramer/AbsHistogramer.hh"
#include "OnlHistogramer/FADCHistogramer.hh"
#include "OnlHistogramer/SADCHistogramer.hh"

#include "DAQ/CupDAQManager.hh"

using namespace std;

void CupDAQManager::TF_RunManager()
{
  fLog->Info("CupDAQManager::TF_RunManager", "run manager started");

  if (!WaitState(fRunStatus, RUNSTATE::kCONFIGURED)) {
    fLog->Warning("CupDAQManager::TF_RunManager", "exited by error state");
    return;
  }

  if (fDAQType == DAQ::TCBDAQ) {
    fTCB->StartTrigger();
    time(&fStartDatime);
    RUNSTATE::SetState(fRunStatus, RUNSTATE::kRUNNING);
  }

  bool iendrun = false;
  std::unique_lock<std::mutex> mlock(fMonitorMutex, std::defer_lock);

  while (true) {
    // for emergent exit
    if (fDoExit) break;

    if (IsForcedEndRunFile()) {
      fLog->Info("CupDAQManager::TF_RunManager",
                 "daq will be ended by ENDRUN command");
      fDoEndRun = true;
      break;
    }

    if (fSetNEvent > 0 || fSetDAQTime > 0) {
      mlock.lock();
      int triggernumber = fTriggerNumber;
      double triggertime = fTriggerTime / kDONESECOND;
      mlock.unlock();

      if (triggernumber >= fSetNEvent) { iendrun = true; }
      if (triggertime >= fSetDAQTime) { iendrun = true; }
      if (iendrun) {
        fLog->Info("CupDAQManager::TF_RunManager",
                   "daq will be ended by preset condition");
        fDoEndRun = true;
        break;
      }
    }

    if (RUNSTATE::CheckError(fRunStatus)) {
      fLog->Warning("CupDAQManager::TF_RunManager", "daq will be ended by error");
      fDoEndRun = true;
      break;
    }

    if (fDoEndRun) {
      fLog->Info("CupDAQManager::TF_RunManager",
                 "daq will be ended by ENDRUN command");
      break;
    }

    if (fDoSplitOutputFile && !fDoSendEvent) {
      if (!OpenNewOutputFile()) { RUNSTATE::SetError(fRunStatus); }
      fDoSplitOutputFile = false;
    }

    gSystem->Sleep(100);
  }

  if (fDAQType == DAQ::TCBDAQ) {
    fTCB->StopTrigger();
    time(&fEndDatime);
    RUNSTATE::SetState(fRunStatus, RUNSTATE::kRUNENDED);
  }

  WaitState(fRunStatus, RUNSTATE::kPROCENDED, false);
  fLog->Info("CupDAQManager::TF_RunManager", "all processes ended");

  PrintDAQSummary();
  fLog->Info("CupDAQManager::TF_RunManager", "run manger ended");
}

void CupDAQManager::TF_TriggerMon()
{
  if (!ThreadWait(fRunStatus, fDoExit)) {
    fLog->Warning("CupDAQManager::TF_TriggerMon", "exited by exit command");
    return;
  }

  fLog->Info("CupDAQManager::TF_TriggerMon", "trigger monitoring started");

  unsigned int dummynevent = 0;
  double dummytime = 0.0;

  std::unique_lock<std::mutex> mlock(fMonitorMutex, std::defer_lock);

  TStopwatch sw;
  sw.Start();

  while (true) {
    double elapsetime = sw.RealTime();
    sw.Continue();

    if (elapsetime > fTriggerMonTime) {
      sw.Start(true);

      mlock.lock();
      int triggernumber = fTriggerNumber;
      double triggertime = fTriggerTime / kDONESECOND;
      mlock.unlock();

      double dtime = triggertime - dummytime;
      double dnevent = triggernumber - dummynevent;

      double insrate = dtime > 0 ? dnevent / dtime : 0.;
      double accrate = triggertime > 0 ? triggernumber / triggertime : 0.;

      fLog->Info("",
                 "%4d events triggered [%8d | %8d / %5.1f(%5.1f) Hz / %.1f s]",
                 int(dnevent), triggernumber, fNBuiltEvent, insrate, accrate,
                 triggertime);

      dummynevent = triggernumber;
      dummytime = triggertime;
    }

    bool runstate = RUNSTATE::CheckState(fRunStatus, RUNSTATE::kRUNENDED) ||
                    RUNSTATE::CheckError(fRunStatus) || fDoExit;
    if (runstate) break;

    gSystem->Sleep(100);
  }

  fLog->Info("CupDAQManager::TF_TriggerMon", "trigger monitoring ended");
}

void CupDAQManager::TF_DebugMon()
{
  if (!ThreadWait(fRunStatus, fDoExit)) {
    fLog->Warning("CupDAQManager::TF_DebugMon", "exited by exit command");
    return;
  }

  int nadc = GetEntries();
  double debugmontime = 1;

  fLog->Info("CupDAQManager::TF_DebugMon", "debug monitoring started");

  TStopwatch sw;
  sw.Start();

  while (true) {
    double elapsetime = sw.RealTime();
    sw.Continue();

    if (elapsetime > debugmontime) {
      sw.Start(true);

      TString adcbcountsize;
      TString adcbufsize;
      TString sortbufsize;

      for (int i = 0; i < nadc; i++) {
        adcbcountsize += Form("%5d ", fRemainingBCount[i]);
        auto * adc = (AbsADC *)fCont[i];
        adcbufsize += Form("%5d ", adc->Bsize());
        ConcurrentDeque<AbsADCRaw *> * modraw = fADCRawBuffers.at(i);
        sortbufsize += Form("%5d ", modraw->size());
      }

      fLog->Debug("DebugMon", "ADC bcount size: %s", adcbcountsize.Data());
      fLog->Debug("DebugMon", "ADC buffer size: %s", adcbufsize.Data());
      fLog->Debug("DebugMon", "sorting buffer size: %s", sortbufsize.Data());
      fLog->Debug("DebugMon", "building buffer size: %5d %5d",
                  fBuiltEventBuffer1.size(), fBuiltEventBuffer2.size());
      fLog->Debug("DebugMon", "%.3f(r) %.3f(s) %.3f(b) %.3f(w)",
                  fReadSleep / 1000., fSortSleep / 1000., fBuildSleep / 1000.,
                  fWriteSleep / 1000.);
    }

    bool runstate = RUNSTATE::CheckState(fRunStatus, RUNSTATE::kRUNENDED) ||
                    RUNSTATE::CheckError(fRunStatus) || fDoExit;
    if (runstate) break;

    gSystem->Sleep(100);
  }

  fLog->Info("CupDAQManager::TF_DebugMon", "debug monitoring ended");
}

void CupDAQManager::TF_MsgServer()
{
  int port = fDAQPort;
  TString name = fDAQName;
  bool istcb = (fDAQID == 0) ? true : false;

  unsigned short daqtag = 0;

  auto * server = new TServerSocket(port, true);
  auto * monitor = new TMonitor();
  monitor->Add(server);

  fLog->Info("CupDAQManager::TF_MsgServer", "[%s] message server start",
             name.Data());

  char buffer[kMESSLEN];
  std::unique_lock<std::mutex> mlock(fMonitorMutex, std::defer_lock);

  while (true) {
    if (!istcb) {
      if (fDoExit) { break; }
    }
    else {
      if (fDoExitTCB) { break; }
    }

    TSocket * socket = monitor->Select(1000);
    if (socket == (TSocket *)-1) { continue; }

    TInetAddress addr = socket->GetInetAddress();

    if (socket->IsA() == TServerSocket::Class()) {
      TSocket * s = ((TServerSocket *)socket)->Accept();
      monitor->Add(s);

      addr = s->GetInetAddress();
      fLog->Info("MsgServer", "[%s] new client connected [ip=%s, port=%d]",
                 name.Data(), addr.GetHostAddress(), addr.GetPort());
      continue;
    }

    int stat = socket->RecvRaw(buffer, kMESSLEN);
    if (stat <= 0) {
      if (stat == 0 || stat == -5) {
        fLog->Info("MsgServer", "[%s] client disconnected [ip=%s, port=%d]",
                   name.Data(), addr.GetHostAddress(), addr.GetPort());
      }
      else {
        fLog->Warning("MsgServer", "[%s] received error (%d) [ip=%s, port=%d]",
                      name.Data(), stat, addr.GetHostAddress(), addr.GetPort());
      }
      socket->Close();
      monitor->Remove(socket);
      delete socket;
    }
    else {
      unsigned short from, to;
      unsigned long command, dummy;
      DecodeMsg(buffer, from, to, command, dummy);

      if (command == kQUERYDAQSTATUS) {
        EncodeMsg(buffer, daqtag, from, istcb ? fRunStatusTCB : fRunStatus);
        socket->SendRaw(buffer, kMESSLEN);
      }
      else if (command == kQUERYRUNINFO) {
        EncodeMsg(buffer, daqtag, from, fRunNumber, fSubRunNumber);
        socket->SendRaw(buffer, kMESSLEN);

        EncodeMsg(buffer, daqtag, from, fStartDatime, fEndDatime);
        socket->SendRaw(buffer, kMESSLEN);
      }
      else if (command == kQUERYTRGINFO) {
        unsigned long nevent, daqtime;

        mlock.lock();
        nevent = fTriggerNumber;
        daqtime = fCurrentTime;
        mlock.unlock();

        EncodeMsg(buffer, daqtag, from, nevent, daqtime);
        socket->SendRaw(buffer, kMESSLEN);
      }
      else if (command == kREQUESTCONFIG) {
        socket->SendObject(fConfigList);
        fLog->Info("MsgServer", "[%s] sent config list to DAQ", name.Data());
      }
      else if (command == kSPLITOUTPUTFILE) {
        if (istcb) { fDoSplitOutputFileTCB = true; }
        else {
          fDoSplitOutputFile = true;
        }
        fLog->Info("MsgServer", "[%s] output file split command received",
                   name.Data());
      }
      else if (command == kRECVEVENT) {
        TMessage * mess;
        if (socket->Recv(mess) > 0) {
          socket->Send("ok");
          auto * event = (BuiltEvent *)mess->ReadObject(mess->GetClass());
          int daqid = event->GetDAQID();
          for (auto buf : fRecvEventBuffer) {
            if (buf.first == daqid) {
              buf.second->push_back(event);
              break;
            }
          }
          delete mess;
        }
        else {
          fLog->Warning("MsgServer",
                        "[%s] error in event sender [ip=%s, port=%d]",
                        name.Data(), addr.GetHostAddress(), addr.GetPort());
        }
      }
      else if (command == kSETERROR) {
        RUNSTATE::SetError(fRunStatus);
      }
      else if (command == kCONFIGRUN) {
        if (istcb) { fDoConfigRunTCB = true; }
        else {
          fDoConfigRun = true;
        }
        fLog->Info("MsgServer", "[%s] CONFIGRUN command received", name.Data());
      }
      else if (command == kSTARTRUN) {
        if (istcb) { fDoStartRunTCB = true; }
        else {
          fDoStartRun = true;
        }
        fLog->Info("MsgServer", "[%s] STARTRUN command received", name.Data());
      }
      else if (command == kENDRUN) {
        if (istcb) { fDoEndRunTCB = true; }
        else {
          fDoEndRun = true;
        }
        fLog->Info("MsgServer", "[%s] ENDRUN command received", name.Data());
      }
      else if (command == kEXIT) {
        if (istcb) { fDoExitTCB = true; }
        else {
          fDoExit = true;
        }
        fLog->Info("MsgServer", "[%s] EXIT command received", name.Data());
      }
      else {
        fLog->Warning("MsgServer", "[%s] Unknown command [%ld] received",
                      name.Data(), command);
      }
    }
  }

  monitor->DeActivateAll();
  TList * sockets = monitor->GetListOfDeActives();
  for (int i = 0; i < sockets->GetSize(); i++) {
    ((TSocket *)(sockets->At(i)))->Close();
    delete ((TSocket *)(sockets->At(i)));
  }

  monitor->RemoveAll();
  delete monitor;

  fLog->Info("CupDAQManager::TF_MsgServer", "[%s] message server ended",
             name.Data());
}

void CupDAQManager::TF_ShrinkToFit()
{
  if (!ThreadWait(fRunStatus, fDoExit)) {
    fLog->Warning("CupDAQManager::TF_ShrinkToFit", "exited by exit command");
    return;
  }

  fLog->Info("CupDAQManager::TF_ShrinkToFit",
             "shrink buffer memory to fit started");

  TStopwatch sw;
  sw.Start();

  int nadc = GetEntries();
  while (true) {
    double elapsetime = sw.RealTime();
    sw.Continue();
    if (elapsetime >= 10) {
      sw.Start(true);

      for (int i = 0; i < nadc; i++) {
        auto * adc = (AbsADC *)fCont[i];
        adc->Bshrink_to_fit();

        ConcurrentDeque<AbsADCRaw *> * modbuffer = fADCRawBuffers.at(i);
        modbuffer->shrink_to_fit();
      }

      fBuiltEventBuffer1.shrink_to_fit();
      fBuiltEventBuffer2.shrink_to_fit();

      if (fRecvEventBuffer.size() > 0) {
        for (auto buf : fRecvEventBuffer) {
          buf.second->shrink_to_fit();
        }
      }

      if (fVerboseLevel > 0) {
        fLog->Info("ShrinkToFit", "shrink buffer memory to fit");
      }
    }

    bool runstate = RUNSTATE::CheckState(fRunStatus, RUNSTATE::kRUNENDED) ||
                    RUNSTATE::CheckError(fRunStatus) || fDoExit;
    if (runstate) break;

    gSystem->Sleep(1000);
  }

  fLog->Info("CupDAQManager::TF_ShrinkToFit",
             "shrink buffer memory to fit ended");
}

void CupDAQManager::TF_SplitOutput(bool ontcb)
{
  TStopwatch sw;

  if (ontcb) {
    if (!ThreadWait(fRunStatusTCB, fDoExitTCB)) { return; }
    fLog->Info("CupDAQManager::TF_SplitOutput", "output splitter started");

    sw.Start();
    while (true) {
      double elapsetime = sw.RealTime();
      sw.Continue();
      if (elapsetime >= fOutputSplitTime) {
        sw.Start(true);
        SendCommandToDAQ(kSPLITOUTPUTFILE);
        fSubRunNumber += 1;

        if (fVerboseLevel >= 1) {
          fLog->Info("SplitOutput", "output file will be split");
        }
      }
      if (fDoEndRunTCB || fRunStatusTCB == kERROR) { break; }
      gSystem->Sleep(10);
    }
  }
  else {
    if (!ThreadWait(fRunStatus, fDoExit)) { return; }
    fLog->Info("CupDAQManager::TF_SplitOutput", "output splitter started");

    sw.Start();
    while (true) {
      double elapsetime = sw.RealTime();
      sw.Continue();
      if (elapsetime >= fOutputSplitTime) {
        sw.Start(true);
        fDoSplitOutputFile = true;
        fSubRunNumber += 1;

        if (fVerboseLevel >= 1) {
          fLog->Info("SplitOutput", "output file will be split");
        }
      }
      bool runstate = RUNSTATE::CheckState(fRunStatus, RUNSTATE::kRUNENDED) ||
                      RUNSTATE::CheckError(fRunStatus) || fDoExit;
      if (runstate) break;

      gSystem->Sleep(10);
    }
  }

  fLog->Info("CupDAQManager::TF_SplitOutput", "output splitter ended");
}

void CupDAQManager::TF_Histogramer()
{
  if (!ThreadWait(fRunStatus, fDoExit)) { return; }  
  fLog->Info("CupDAQManager::TF_Histogramer", "histogramer started");

  //
  // Create histogramer
  //
  AbsHistogramer * histogramer = nullptr;
  switch (fADCType) {
    case ADC::FADCT: histogramer = new FADCHistogramer(); break;
    case ADC::FADCS: histogramer = new FADCHistogramer(); break;
    case ADC::GADCT: histogramer = new FADCHistogramer(); break;
    case ADC::GADCS: histogramer = new FADCHistogramer(); break;
    case ADC::MADCS: histogramer = new FADCHistogramer(); break;
    case ADC::SADCT: histogramer = new SADCHistogramer(); break;
    case ADC::SADCS: histogramer = new SADCHistogramer(); break;
    default: break;
  }
  histogramer->SetRunNumber(fRunNumber);
  histogramer->SetADCType(fADCType);
  histogramer->SetConfigList(fConfigList);
  histogramer->SetStartDatime(fStartDatime);

  // open histogramer root file
  if (fHistFilename.IsNull()) {
    TString filename;
    TString dirname = gSystem->Getenv("RAWDATA_DIR");
    if (dirname.IsNull()) {
      fLog->Warning("CupDAQManager::TF_Histogramer",
                    "variable RAWDATA_DIR is not set");
      filename = Form("hist_%s_%06d.root", GetADCName(fADCType), fRunNumber);
    }
    else {
      dirname += Form("/HIST/%06d", fRunNumber);
      int isdir = gSystem->Exec(Form("test -d %s", dirname.Data()));
      if (!isdir) {
        gSystem->Exec(Form("mkdir %s", dirname.Data()));
        fLog->Info("CupDAQManager::TF_Histogramer", "%s created",
                   dirname.Data());
      }
      fLog->Info("CupDAQManager::TF_Histogramer", "%s already exist",
                 dirname.Data());

      filename = Form("%s/hist_%s_%06d.root", dirname.Data(),
                      GetADCName(fADCType), fRunNumber);
    }
    histogramer->SetFilename(filename);
    fHistFilename = filename;
  }

  if (!histogramer->Open()) {
    fLog->Warning(
        "CupDAQManager::TF_Histogramer",
        "Can\'t open histogramer root file %s, histogramer will be ended",
        fHistFilename.Data());
    return;
  }

  // booking histograms
  histogramer->Book();

  int eventnumber = 0;
  int ntotalmonitoredevent = 0;

  double trate = 0.3;

  double perror = 0;
  double integral = 0;

  fBenchmark->Start("Histogramer");
  while (true) {
    if (fBuildStatus == ENDED) {
      if (fBuiltEventBuffer2.empty()) break;
    }

    if (fRunStatus == kERROR || fDoExit) { break; }

    BuiltEvent * builtevent = nullptr;

    int size = fBuiltEventBuffer2.size();

    if (size > 0) { builtevent = fBuiltEventBuffer2.popfront(); }

    if (builtevent) {
      eventnumber = builtevent->GetEventNumber();
      if (gRandom->Rndm() < trate) {
        histogramer->Fill(builtevent);
        histogramer->Update();
        ntotalmonitoredevent += 1;
      }
      delete builtevent;
    }

    ThreadSleep(fHistSleep, perror, integral, size);
  }
  fBenchmark->Stop("Histogramer");

  histogramer->Close();
  delete histogramer;

  fHistogramerEnded = true;

  fLog->Info("CupDAQManager::TF_Histogramer",
             "total monitored event = %d (%.2f%%)", ntotalmonitoredevent,
             100. * ntotalmonitoredevent / double(eventnumber));
  fLog->Info("CupDAQManager::TF_Histogramer", "online histogramer ended");
}
