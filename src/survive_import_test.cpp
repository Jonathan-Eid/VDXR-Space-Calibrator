// SPDX-License-Identifier: MIT
// No device startup: exercise the real native database importer on Windows.
#define SURVIVE_IMPORT __declspec(dllimport)
#include <libsurvive/survive.h>
extern "C" __declspec(dllimport) int survive_load_steamvr_lighthousedb_from_file(SurviveContext*,const char*);
#include <cmath>
#include <fstream>
#include <iostream>
#include <cstdio>
int main(){
    char program[]="import-test",flag[]="--configfile",config[]="./import-test-config.json";char* args[]{program,flag,config};SurviveContext* ctx=survive_init(3,args);if(!ctx)return 1;
    ctx->activeLighthouses=2;ctx->bsd[0].BaseStationID=123;ctx->bsd[1].BaseStationID=3783486585u;for(int i=0;i<2;++i){ctx->bsd[i].accel[2]=1;ctx->bsd[i].Pose.Rot[0]=1;}
    {std::ofstream file("import-test-db.json",std::ios::binary);file<<"{\r\n\"known_universes\":[{\"base_stations\":[{\"base_serial_number\":\"123\",\"target_pose\":{\"pose\":[0,0,0,1,1,0,0]}},{\"base_serial_number\":\"3783486585\",\"target_pose\":{\"pose\":[0,0,0,1,2,0,0]}}]}]}\r\n";}
    const int result=survive_load_steamvr_lighthousedb_from_file(ctx,"import-test-db.json");
    const bool correct=result==0&&ctx->bsd[0].PositionSet&&ctx->bsd[1].PositionSet&&std::abs(ctx->bsd[0].Pose.Pos[0]+1)<1e-9&&std::abs(ctx->bsd[1].Pose.Pos[0]+2)<1e-9;
    {std::ofstream file("import-test-db.json",std::ios::binary);file<<"{]";}
    const bool errorPropagated=survive_load_steamvr_lighthousedb_from_file(ctx,"import-test-db.json")<0;
    if(!correct||!errorPropagated)std::cerr<<"result="<<result<<" positions="<<ctx->bsd[0].Pose.Pos[0]<<","<<ctx->bsd[1].Pose.Pos[0]<<" set="<<ctx->bsd[0].PositionSet<<","<<ctx->bsd[1].PositionSet<<" error_propagated="<<errorPropagated<<'\n';
    survive_close(ctx);std::remove("import-test-db.json");std::remove("import-test-config.json");
    if(!correct||!errorPropagated){std::cerr<<"Database import failed CRLF, unsigned ID, or parse-error propagation test\n";return 1;}
    std::cout<<"Passed actual importer: CRLF input, unsigned 32-bit station ID, parse-error propagation; no device startup\n";return 0;
}
