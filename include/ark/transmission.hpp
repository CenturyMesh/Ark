#pragma once
#include "ark/model.hpp"
namespace ark {
class AgentSession;
// Existing mate expressions remain the single authored source for motion and
// engineering calculations. Reports are derived and never certify tooth contact.
bool isTransmissionMate(const std::string&);
bool transmissionMotionField(const std::string& type,const std::string& key);
bool extendedTransmissionDocument(const Document&);
arkjson::Value evaluateTransmissionPair(const std::string& type,const arkjson::Value& numeric,
                                      double observedCenterMm=-1,double axialOffsetMm=0);
arkjson::Value evaluateTransmissionMate(const AssemblyMate&,const ExprEnv&,
                                      double observedCenterMm=-1,double axialOffsetMm=0);
arkjson::Value transmissionLoad(const arkjson::Value& pair,double inputRpm,double inputTorqueNm);
arkjson::Value analyzeTransmission(const Document&,u64 revision,const arkjson::Value& args);
arkjson::Value designTransmission(const Document&,u64 revision,const arkjson::Value& args);
bool callTransmissionTool(AgentSession&,const std::string&,const arkjson::Value&,arkjson::Value&);
void appendTransmissionTools(arkjson::Value&);
arkjson::Value transmissionCapabilities();
}
