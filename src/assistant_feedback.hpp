#pragma once
// 076A run-scoped tool_feedback gate shared by the HTTP, ACP and Claude Code
// connectors. The 012D contract is unchanged: agents rate their actual tool
// use and the host never supplies scores. The gate covers the activity IDs that
// this run's receipts created. Unrated rows that earlier conversations,
// providers or interrupted runs left under the same actor id stay pending in
// the project ledger as non-blocking backlog: they are counted, never demanded.
// No turn or token budget: another reminder follows only while this run's
// unrated count is lower than at the previous reminder. New actions raise the
// count and ratings of rows outside the run never lower it, so every continued
// reminder lowers a finite count and the loop ends. Otherwise the run ends with
// its reply, an explicit note and the rows still pending; a team role's pending
// rows keep its task and the team goal from being completed.
#include "ark/json.hpp"
#include <algorithm>
#include <string>
#include <vector>
namespace ark::assistant_feedback {
using V=arkjson::Value;
struct RunGate {
    enum class Step {Finish,Remind,Unresolved};
    std::vector<std::string> pending; // this run's unrated activity IDs, in creation order
    std::vector<std::string> dropped; // this run's unrated IDs left behind at a project boundary (first 64)
    std::string project;
    std::size_t created=0,rated=0,droppedCount=0,reminders=0,remindedPending=0,ledgerPending=0;
    bool counted=false,reported=false,team=false;
    static const V& payload(const V& receipt){const auto* data=receipt.find("structuredContent");return data&&data->t==V::Obj?*data:receipt;}
    void action(const V& data){
        if(data.t!=V::Obj)return;const auto* required=data.find("feedback_required");
        if(const auto* id=data.find("activity_id");id&&id->t==V::Str&&!id->str.empty()&&required&&required->t==V::Bool&&required->b&&std::find(pending.begin(),pending.end(),id->str)==pending.end()){pending.push_back(id->str);++created;}
    }
    void resolve(const V* ids){if(ids&&ids->t==V::Arr)for(const auto& id:ids->arr)if(id.t==V::Str)if(const auto at=std::find(pending.begin(),pending.end(),id.str);at!=pending.end()){pending.erase(at);++rated;}}
    // Live receipts of this run only; retained history is never replayed here.
    void observe(const V& receipt){
        const auto& data=payload(receipt);if(data.t!=V::Obj)return;
        if(const auto* p=data.find("activity_project_id");p&&p->t==V::Str){const auto* boundary=data.find("activity_boundary");
            if(p->str!=project||(boundary&&boundary->t==V::Bool&&boundary->b)){for(const auto& id:pending)if(dropped.size()<64)dropped.push_back(id);droppedCount+=pending.size();pending.clear();counted=false;}
            project=p->str;}
        action(data);
        // Verification reads that team tools perform for the role are its actions too.
        if(const auto* nested=data.find("revision_verification"))action(payload(*nested));
        if(const auto* checkpoint=receipt.find("initial_checkpoint_verification");checkpoint&&checkpoint->t==V::Obj)if(const auto* inspection=checkpoint->find("inspection"))action(payload(*inspection));
        // Only IDs of this run count; an ID reported as already rated is resolved.
        resolve(data.find("rated_ids"));resolve(data.find("already_rated_ids"));
        if(const auto* count=data.find("pending_feedback_count");count&&count->t==V::Num&&count->num>=0){ledgerPending=(std::size_t)count->num;counted=true;}
    }
    Step step(bool available){
        if(pending.empty()||!available)return Step::Finish;
        if(!reminders||pending.size()<remindedPending){++reminders;remindedPending=pending.size();reported=false;return Step::Remind;}
        return Step::Unresolved;
    }
    // Last observed unrated rows of this actor outside the run's pending set.
    std::size_t backlog()const{return counted&&ledgerPending>pending.size()?ledgerPending-pending.size():0;}
    static std::string actions(std::size_t n){return std::to_string(n)+(n==1?" action":" actions");}
    std::string reminder(bool teamRole)const{
        V ids=V::makeArr();for(const auto& id:pending)ids.arr.push_back(V::makeStr(id));
        std::string text="Before finishing, submit tool_feedback for the "+actions(pending.size())+" of this run that are still unrated: "+ids.dump()+". Rate utility and difficulty 1..5, describe the actual action, and note issues or wished tools/options; at most 64 ratings per call, which can be batched with other tool calls. Do not invent successful outcomes. A tool call made after this reminder is a new action of this run; ARK repeats this reminder only while the number of unrated actions of this run falls.";
        if(teamRole)text+=" In a team, the same ratings can go in team_report_task or team_finish_goal ratings; unrated actions keep your task and the team goal from being completed.";
        if(const auto older=backlog())text+=" "+std::to_string(older)+" older unrated project "+(older==1?"record from an earlier run or conversation is":"records from earlier runs or conversations are")+" not required here; rate one only if you can describe that actual action (activity_info with pending_only lists them).";
        return text;
    }
    std::string status()const{return "Feedback pending for "+actions(pending.size())+" of this run; work and records retained";}
    std::string note()const{const auto older=backlog();
        return "ARK: tool feedback is still pending for "+actions(pending.size())+" of this run. CAD work and activity records are retained; activity_info with pending_only lists them."+
            (team?std::string(" In this team, this role's task report and the team goal cannot be completed until they are rated (tool_feedback or report ratings)."):std::string())+
            (older?" "+std::to_string(older)+" older pending "+(older==1?"record remains":"records remain")+" as non-blocking backlog.":std::string());}
    // Host-observed outcome for AssistantResult::feedback and the retained record.
    // run_actions = run_rated + run_pending + run_boundary_dropped.
    V summary()const{
        V out=V::makeObj(),ids=V::makeArr(),left=V::makeArr();for(const auto& id:pending)ids.arr.push_back(V::makeStr(id));for(const auto& id:dropped)left.arr.push_back(V::makeStr(id));
        out.set("scope",V::makeStr("this_run"));out.set("state",V::makeStr(!pending.empty()?"pending":droppedCount?"boundary_dropped":created?"complete":"no_actions"));
        out.set("run_actions",V::makeNum((double)created));out.set("run_rated",V::makeNum((double)rated));out.set("run_pending",V::makeNum((double)pending.size()));out.set("run_pending_ids",ids);
        out.set("run_boundary_dropped",V::makeNum((double)droppedCount));out.set("run_boundary_dropped_ids",left);
        out.set("backlog_pending",V::makeNum((double)backlog()));out.set("reminders",V::makeNum((double)reminders));out.set("team_role",V::makeBool(team));out.set("blocks_completion",V::makeBool(team&&!pending.empty()));if(!project.empty())out.set("activity_project_id",V::makeStr(project));
        out.set("authority",V::makeStr("host-observed tool receipts; ratings are subjective agent self-reports"));return out;
    }
    // Retained as data for later runs. A Single run's rows become backlog; a team
    // role's rows keep blocking its task and the goal until that role rates them.
    V record()const{V out=V::makeObj();out.set("role",V::makeStr("developer"));out.set("content",V::makeStr("ARK_FEEDBACK_PENDING_V1: this run ended with tool_feedback still missing for the listed actions. The rows stay pending in the project ledger"+std::string(team?"; in this team they block this role's task report and the goal until rated. ":" and do not block later runs. ")+summary().dump()));return out;}
};
// The reply that triggered the first reminder stays visible before the final
// reply; a leading needs_input:/review_pending: state of the final reply is kept first.
inline std::string withHeldReply(const std::string& held,const std::string& reply){
    if(held.empty()||held==reply)return reply;if(reply.empty())return held;
    const auto first=reply.find_first_not_of(" \t\r\n");
    for(const std::string prefix:{"needs_input:","review_pending:"})if(first!=std::string::npos&&reply.compare(first,prefix.size(),prefix)==0){auto rest=reply.substr(first+prefix.size());if(!rest.empty()&&rest.front()==' ')rest.erase(0,1);return prefix+" "+held+"\n\n"+rest;}
    return held+"\n\n"+reply;
}
}
