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
 declare(path,id,"generation-A",101);auto first=id.RefreshSpace(101);check(first.known&&first.revision==1,"explicit first adoption");
 for(const char* role:{"provider","receiver","service","GUI"}){ObservationIdentity restart(path);auto state=restart.RefreshSpace(101);check(state.known&&state.revision==first.revision,role);}
 declare(path,id,"generation-A",202);check(id.RefreshSpace(202).revision==first.revision,"sender restart same declaration");
 declare(path,id,"generation-B",202);auto changed=id.RefreshSpace(202);check(changed.known&&changed.revision==2,"explicit generation change");
 check(id.RefreshSpace(202).revision==2,"idempotent declaration");
 auto unknown=id.RefreshSpace(303);check(!unknown.known&&unknown.revision==3,"unproven sender stops old world");
 {ObservationIdentity restart(path);check(!restart.SpaceKnown()&&restart.RefreshSpace(303).revision==3,"unknown restart no drift");}
 declare(path,id,"generation-B",303);check(id.RefreshSpace(303).known&&id.Revision()==3,"known recovery retains invalidated revision");
 {std::ofstream abandoned(path.string()+".new-crash");abandoned<<"partial";}
 {ObservationIdentity afterCrash(path);check(afterCrash.Revision()==3&&afterCrash.SpaceKnown(),"uncommitted crash temporary ignored");}
 auto corrupt=root/"corrupt";{std::ofstream f(corrupt);f<<"pico-observation-identity-v2\npartial";}rejects([&]{ObservationIdentity bad(corrupt);});
 auto overflow=root/"overflow";{std::ofstream f(overflow);f<<"pico-observation-identity-v2\n"<<id.SourceId()<<'\n'<<id.SpaceId()<<"\n4294967295\ngeneration-B\nknown\n";}
 {ObservationIdentity end(overflow);check(end.Revision()==UINT32_MAX,"restart at maximum allowed");declare(overflow,end,"generation-C",303);rejects([&]{end.RefreshSpace(303);});ObservationIdentity recovered(overflow);check(recovered.Revision()==UINT32_MAX&&recovered.Generation()=="generation-B","overflow preserves complete preimage");}
 std::cout<<"PASS clean start; provider/receiver/service/GUI restart; declared sender restart; changed; unknown; recovery; persistence; abandoned crash temporary; corrupt state; overflow\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
