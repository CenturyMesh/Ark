#pragma once
#include "ark/model.hpp"
#include <string>
namespace ark {
// 049A explicit migration of 044B-048B documents whose authoring and journal hold
// inline baked frames. Every bake in the current authoring and in every journal
// state becomes a receipt; the journal is rebuilt as schema 2 with new digests and
// node ids, and the migration is recorded in its migrations list. The current
// frames are cached. The source document is never edited; failures throw
// HISTORY_MIGRATION_* and leave nothing changed.
struct DerivedBakeMigration {
    size_t nodes=0,states=0,payloadReferences=0,payloadBytes=0,blobsBefore=0,blobsAfter=0;
    size_t journalBytesBefore=0,journalBytesAfter=0,documentBytesBefore=0,documentBytesAfter=0,framesCached=0;
    std::string headBefore,headAfter,baselineBefore,baselineAfter;
};
Document migrateDerivedBakes(const Document& source,const arkjson::Value& actor,const std::string& motivation,const arkjson::Value& backup,DerivedBakeMigration& report);
// history_migrate_derived{path,expected_revision,confirm:true,motivation?,actor?}. path is the saved .ark
// document; the complete pre-migration document is first written beside it as
// <stem>.pre-049A.ark (or -2..-99, never overwriting) and reopened to verify it.
// The migration then replaces the session document and advances revision; saving stays explicit.
arkjson::Value historyMigrateDerivedTool(Document& document,u64& revision,const arkjson::Value& args);
}
