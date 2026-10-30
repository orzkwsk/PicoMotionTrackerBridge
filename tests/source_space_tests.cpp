#include "pico_ot_backend/observation_identity.hpp"
#include <fstream>
#include <iostream>
#include <functional>
#include <limits>
#include <stdexcept>
using namespace pico_ot;
void check(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
void rejects(const std::function<void()>& f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,"expected fail closed");}
void declare(const std::filesystem::path& path,const ObservationIdentity& id,const std::string& generation,uint64_t sender,const char* status="known"){
 std::ofstream f(path.string()+".space");f<<"pico-source-space-declaration-v1\n"<<id.SourceId()<<'\n'<<id.SpaceId()<<'\n'<<generation<<'\n'<<sender<<'\n'<<status<<'\n';
}
int main(int argc,char** argv){try{
 check(argc==2,"fixture root required");auto root=std::filesystem::path(argv[1])/NewObservationUuid();std::filesystem::create_directories(root);auto path=root/"identity.conf";
 ObservationIdentity id(path);check(id.Revision()==0&&!id.SpaceKnown(),"clean start unknown");
 auto missing=id.RefreshSpace(101);check(!missing.known&&missing.revision==0,"missing quality metadata changed identity generation");
 declare(path,id,"generation-A",101);auto first=id.RefreshSpace(101);check(first.known&&first.revision==1,"explicit first adoption");
 for(const char* role:{"provider","receiver","service","GUI"}){ObservationIdentity restart(path);auto state=restart.RefreshSpace(101);check(state.known&&state.revision==first.revision,role);}
 declare(path,id,"generation-A",202);check(id.RefreshSpace(202).revision==first.revision,"sender restart same declaration");
 declare(path,id,"generation-B",202);auto changed=id.RefreshSpace(202);check(changed.known&&changed.revision==2,"explicit generation change");
 check(id.RefreshSpace(202).revision==2,"idempotent declaration");
 auto unknown=id.RefreshSpace(303);check(!unknown.known&&unknown.revision==2,"transport uncertainty advanced map generation");
 {ObservationIdentity restart(path);check(!restart.SpaceKnown()&&restart.RefreshSpace(303).revision==2,"unknown restart no drift");}
 declare(path,id,"generation-B",303);check(id.RefreshSpace(303).known&&id.Revision()==2,"known recovery changed generation");
 {std::ofstream abandoned(path.string()+".new-crash");abandoned<<"partial";}
 {ObservationIdentity afterCrash(path);check(afterCrash.Revision()==2&&afterCrash.SpaceKnown(),"uncommitted crash temporary ignored");}
 const auto source=id.SourceId(),space=id.SpaceId();
 {std::ofstream broken(path.string()+".space");broken<<"partial optional quality metadata";}
 check(!id.RefreshSpace(303).known&&id.Revision()==2&&id.SourceId()==source&&id.SpaceId()==space,
       "malformed optional quality metadata blocked stable identity");
 declare(path,id,"generation-B",303);check(id.RefreshSpace(303).known,"restore diagnostic baseline");
 // Simulate an atomic-replacement I/O failure without changing the saved
 // identity bytes: retain the preimage and obstruct only the test-owned target.
 auto preimage=root/"identity-preimage.conf";std::filesystem::rename(path,preimage);
 std::filesystem::create_directory(path);{std::ofstream occupied(path/"occupied");occupied<<"test-owned replacement obstruction";}
 declare(path,id,"generation-C",303);auto failed=id.RefreshSpace(303);
 check(!failed.known&&!failed.metadataPersisted&&failed.revision==2&&id.Generation()=="generation-B"&&id.SourceId()==source,
       "optional quality write failure changed stable identity/high-water or blocked collection");
 std::filesystem::remove(path/"occupied");std::filesystem::remove(path);std::filesystem::rename(preimage,path);
 {ObservationIdentity durable(path);check(durable.Revision()==2&&durable.Generation()=="generation-B"&&durable.SpaceKnown(),
    "quality write failure damaged durable preimage");}
 auto corrupt=root/"corrupt";{std::ofstream f(corrupt);f<<"pico-observation-identity-v2\npartial";}rejects([&]{ObservationIdentity bad(corrupt);});
 auto overflow=root/"overflow";{std::ofstream f(overflow);f<<"pico-observation-identity-v2\n"<<id.SourceId()<<'\n'<<id.SpaceId()<<"\n4294967295\ngeneration-B\nknown\n";}
 {ObservationIdentity end(overflow);check(end.Revision()==UINT32_MAX,"restart at maximum allowed");declare(overflow,end,"generation-C",303);auto state=end.RefreshSpace(303);check(!state.known&&state.revision==UINT32_MAX,"diagnostic generation overflow blocked or wrapped identity");rejects([&]{end.AdvanceRevision();});ObservationIdentity recovered(overflow);check(recovered.Revision()==UINT32_MAX&&recovered.Generation()=="generation-B","overflow preserves identity/high-water");}
 std::cout<<"PASS clean start; provider/receiver/service/GUI restart; declared sender restart; changed; unknown; recovery; persistence; abandoned crash temporary; corrupt state; overflow\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
