#include "save_manager.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>
namespace fs=std::filesystem;
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void Write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<text;}
std::string Read(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv)try {
 if(argc!=2)return 2;const fs::path root=argv[1];fs::create_directories(root);
 const auto data=root/"nand/title/00010000/524d4850/data";
 Write(data/"nocopy/data00","original progress");Write(data/"banner.bin","banner");
 std::vector<fs::path> original;
 for(int i=0;i<4;++i) {
  Write(data/"nocopy/data00","progress "+std::to_string(i));
  auto result=mh3_save_manager::CreateBackup(data,root/"saves");Check(result.success,"backup failed");original.push_back(result.output);
  Check(Read(result.output/"data/nocopy/data00")==Read(data/"nocopy/data00"),"backup differs from saved files");
  std::this_thread::sleep_for(std::chrono::milliseconds(3));
 }
 auto backups=mh3_save_manager::ListSaveCopies(root,true);
 Check(backups.size()==3 && !fs::exists(original[0]) && fs::exists(original[1]),"fourth backup did not rotate only oldest");
 Check(backups[0].folder==original[3] && backups[2].folder==original[1],"backups not newest first");
 Check(mh3_save_manager::CopyCreatedTime("Backup-20261025-184512-123")=="25 Oct 2026, 06:45:12 PM (local time)","human readable creation time incorrect");
 auto failed=mh3_save_manager::CreateBackup(root/"missing",root/"saves");
 Check(!failed.success && mh3_save_manager::ListSaveCopies(root,true).size()==3,"failed backup removed earlier copies");
 fs::create_directories(root/"exports");
 auto exported=mh3_save_manager::ExportSave(data,root/"exports");Check(exported.success,"export failed");
 Check(Read(exported.output/"title/00010000/524d4850/data/nocopy/data00")=="progress 3","export did not contain latest in-game save");
 Check(Read(exported.output/"README.txt").find("Restore / import")!=std::string::npos,"export instructions missing");
 Check(mh3_save_manager::ListSaveCopies(root,false).size()==1,"export browser entry missing");
 const auto oldest=backups.back().folder;
 const auto chosenProgress=Read(oldest/"data/nocopy/data00");
 Write(data/"nocopy/data00","current progress before restore");
 auto queued=mh3_save_manager::QueueRestore(oldest,root);Check(queued.success,"could not stage oldest backup");
 Check(Read(data/"nocopy/data00")=="current progress before restore","queue changed the live save before restart");
 auto restored=mh3_save_manager::CompleteQueuedRestore(root);Check(restored.success,"restore on startup failed");
 Check(Read(data/"nocopy/data00")==chosenProgress,"restore used a different copy");
 Check(!fs::exists(root/"PendingRestore") && mh3_save_manager::ListSaveCopies(root,true).size()==3,"restore did not clear request or respect backup cap");
 bool savedPrevious=false;
 for(const auto& copy:mh3_save_manager::ListSaveCopies(root,true))if(Read(copy.folder/"data/nocopy/data00")=="current progress before restore")savedPrevious=true;
 Check(savedPrevious,"restore did not preserve previous saved progress");
 Check(fs::exists(exported.output),"backup retention deleted an export");
 Check(mh3_save_manager::CompleteQueuedRestore(root).detail=="no-queued-restore","restore ran twice");
 Write(root/"PendingRestore/data/nocopy/data00","unconfirmed staged files");
 Check(mh3_save_manager::CompleteQueuedRestore(root).detail=="no-queued-restore" && Read(data/"nocopy/data00")==chosenProgress,"unconfirmed files were imported");
 auto bad=mh3_save_manager::QueueRestore(root/"invalid-folder",root);
 Check(!bad.success && Read(data/"nocopy/data00")==chosenProgress,"invalid import changed saved progress");
 const auto blocked=root/"blocked-restore",blockedData=blocked/"nand/title/00010000/524d4850/data";
 Write(blockedData/"nocopy/data00","keep this saved progress");Write(blocked/"saves","not a directory");
 Check(mh3_save_manager::QueueRestore(exported.output,blocked).success,"failure fixture could not stage an import");
 Check(!mh3_save_manager::CompleteQueuedRestore(blocked).success && Read(blockedData/"nocopy/data00")=="keep this saved progress",
       "failed safety backup replaced current progress");
 Check(mh3_save_manager::CompleteQueuedRestore(blocked).detail=="no-queued-restore","failed restore would repeat at every startup");
 std::cout<<"Save manager: cap/rotation, failed-copy safety, readable times, export, explicit staged restore and automatic backup checks passed.\n";
 return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
