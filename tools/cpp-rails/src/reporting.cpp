#include "cpp_rails.hpp"
#include "detail.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <sstream>
#include <iomanip>
#include <cctype>
namespace cpprails {
using detail::json_quote; using detail::table_safe;
namespace {
struct RuleTally {std::size_t production_p2=0,production_p3=0,test=0;};
std::string metrics_json(const Report& r) {
    std::ostringstream o;
    o<<"{\"code_lines\":"<<r.code_lines<<",\"production_lines\":"<<r.production_lines<<",\"test_lines\":"<<r.test_lines<<",\"per_kloc\":";
    if(r.code_lines)o<<1000.0*static_cast<double>(r.findings.size())/static_cast<double>(r.code_lines);else o<<"null";
    o<<",\"by_rule\":{";std::map<std::string,std::size_t> counts;
    for(const auto& f:r.findings)++counts[f.rule];bool first=true;
    for(const auto& [rule,count]:counts) {if(!first)o<<',';first=false;o<<json_quote(rule)<<":{\"count\":"<<count<<",\"per_kloc\":";
        if(r.code_lines)o<<1000.0*static_cast<double>(count)/static_cast<double>(r.code_lines);else o<<"null";o<<'}';}
    o<<"}}";return o.str();
}
std::string delta_json(const Report& r) {
    std::ostringstream o;o<<"{\"loaded\":"<<(r.delta.loaded?"true":"false")<<",\"new\":"<<r.delta.added<<",\"known\":"<<r.delta.known
      <<",\"unobserved\":"<<r.delta.unobserved<<",\"resolved\":[";
    for(std::size_t i=0;i<r.delta.resolved.size();++i) {if(i)o<<',';const auto& f=r.delta.resolved[i];
        o<<"{\"id\":"<<json_quote(f.id)<<",\"path\":"<<json_quote(f.path)<<",\"line\":"<<f.line<<",\"evidence\":"<<json_quote(f.evidence)<<",\"rule\":"<<json_quote(f.rule)<<",\"analyzer\":"<<json_quote(f.analyzer)<<'}';}
    o<<"],\"by_rule\":{";std::map<std::string,std::array<std::size_t,4>> rules;std::set<std::string> paths;
    for(const auto& f:r.findings){++rules[f.rule][f.existing?1:0];paths.insert(f.path);}
    for(const auto& f:r.delta.resolved){++rules[f.rule][2];paths.insert(f.path);}
    for(const auto& f:r.delta.unobserved_entries)++rules[f.rule][3];bool first=true;
    for(const auto& [rule,counts]:rules){if(!first)o<<',';first=false;o<<json_quote(rule)<<":{\"new\":"<<counts[0]<<",\"known\":"<<counts[1]<<",\"resolved\":"<<counts[2]<<",\"unobserved\":"<<counts[3]<<'}';}
    o<<"},\"affected_paths\":[";first=true;for(const auto& path:paths){if(!first)o<<',';first=false;o<<json_quote(path);}o<<"]}";return o.str();
}
std::map<std::string,RuleTally> tally_by_rule(const Report& r) {
    std::map<std::string,RuleTally> out;
    for (auto& f:r.findings) {
        if (f.suppressed) continue;
        auto& t=out[f.rule];
        if (f.scope=="test") ++t.test;
        else if (f.severity=="P2") ++t.production_p2;
        else ++t.production_p3;
    }
    return out;
}
// Production files with the most unsuppressed hints: where a human reviewer should start.
std::vector<std::pair<std::string,std::size_t>> hotspots(const Report& r,std::size_t limit=10) {
    std::map<std::string,std::size_t> counts;
    for (auto& f:r.findings) if (!f.suppressed && f.scope!="test" && f.severity=="P2") ++counts[f.path];
    std::vector<std::pair<std::string,std::size_t>> ranked(counts.begin(),counts.end());
    std::stable_sort(ranked.begin(),ranked.end(),[](auto& a,auto& b){return a.second>b.second;});
    if (ranked.size()>limit) ranked.resize(limit);
    return ranked;
}
std::string uri_escape(const std::string& s) {
    std::ostringstream o;
    for (unsigned char c:s) {
        if (std::isalnum(c)||c=='/'||c=='-'||c=='_'||c=='.'||c=='~'||c==':') o<<c;
        else o<<'%'<<std::uppercase<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<int>(c)<<std::nouppercase<<std::dec;
    }
    return o.str();
}
} // namespace

std::string markdown(const Report& r) {
    std::ostringstream out;
    const auto production=std::count_if(r.findings.begin(),r.findings.end(),[](auto& f){return f.scope!="test";});
    out << "# cpp-engineering-guardrails — " << r.action << "\n\n"
        << "- Root: `" << r.root << "`\n- Profile: `" << r.profile << "`\n"
        << "- Files scanned: " << r.files_scanned << "; skipped: " << r.files_skipped << "\n"
        << "- Review findings: " << r.findings.size() << " (" << production << " production, "
        << (r.findings.size()-static_cast<std::size_t>(production)) << " test; "
        << r.new_unsuppressed_p2() << " new, unsuppressed P2)"
        << (r.findings_capped?"; "+std::to_string(r.findings_capped)+" more not listed (per-file limit)":"") << "\n\n";
    out<<"- Code-bearing lines: "<<r.code_lines<<" (production "<<r.production_lines<<", tests "<<r.test_lines<<"); findings/1,000 lines: ";
    if(r.code_lines)out<<1000.0*static_cast<double>(r.findings.size())/static_cast<double>(r.code_lines);else out<<"UNMEASURED";
    out<<". Rate describes selected scope, not software quality.\n\n";
    if(r.delta.loaded)out<<"Baseline: "<<r.delta.added<<" new; "<<r.delta.known<<" known; "<<r.delta.resolved.size()<<" disappeared in observed scope; "<<r.delta.unobserved<<" unobserved.\n\n";
    for(const auto& section:r.sections)out<<section.markdown<<"\n\n";
    const auto tallies=tally_by_rule(r);
    if (!tallies.empty()) {
        out << "## Summary by rule\n\n| Rule | Production P2 | Production P3 | Test (P3) |\n|---|---|---|---|\n";
        std::vector<std::pair<std::string,RuleTally>> rows(tallies.begin(),tallies.end());
        std::stable_sort(rows.begin(),rows.end(),[](auto& a,auto& b){return a.second.production_p2>b.second.production_p2;});
        for (auto& [rule,t]:rows) out << "| " << rule << " | " << t.production_p2 << " | " << t.production_p3 << " | " << t.test << " |\n";
        const auto hot=hotspots(r);
        if (!hot.empty()) {
            out << "\n**Review first (production files with most P2 hints):** ";
            for (std::size_t i=0;i<hot.size();++i) out << (i?", ":"") << '`' << table_safe(hot[i].first) << "` (" << hot[i].second << ")";
            out << "\n";
        }
        out << "\n";
    }
    out << "## Verification gates\n\n| Gate | Status | Required | Evidence |\n|---|---|---|---|\n";
    if (r.gates.empty()) out << "| — | UNVERIFIED | no | No external verification requested |\n";
    for (auto& g:r.gates) out << "| " << table_safe(g.name) << " | **" << status_text(g.status) << "** | "
        << (g.required?"yes":"no") << " | " << table_safe(g.details.substr(0,450)) << " |\n";
    out << "\n## Findings (review candidates, NOT proven defects)\n\n| Priority | Rule / ID | Location | Scope | Confidence | State |\n|---|---|---|---|---|---|\n";
    std::size_t shown=0;
    for (auto& f:r.findings) {
        if (++shown>200) break;
        out << "| " << f.severity << " | " << table_safe(f.rule) << "/`" << f.id << "` | `"
            << table_safe(f.path) << "`:" << f.line << " | " << f.scope << " | " << f.confidence << " / "<<table_safe(f.analyzer)<<" / "<<table_safe(f.evidence_level)<<" | "
            << (f.suppressed?"suppressed":(f.existing?"baseline":"new")) << " |\n";
    }
    if (r.findings.size()>200) out << "\nFirst 200 of " << r.findings.size() << " findings shown; use JSON for all.\n";
    out << "\n### Review instructions (first 25)\n";
    for (std::size_t j=0;j<std::min<std::size_t>(r.findings.size(),25);++j) {
        auto& f=r.findings[j];
        out << "- **"<<f.id<<"** "<<table_safe(f.message)<<" (`"<<table_safe(f.path)<<"`:"<<f.line<<"). Evidence `"
            <<table_safe(f.evidence)<<"`. "<<table_safe(f.recommendation)<<"\n";
    }
    out << "\n## Notes and limitations\n";
    for (auto& note:r.notes) out << "- " << note << "\n";
    out << "\nScanner hints are not semantic proof. Missing tool checks are UNVERIFIED, not green. This report does not certify correctness, memory safety or ABI stability.\n";
    return out.str();
}
std::string json(const Report& r) {
    std::ostringstream out;
    out<<"{\"action\":"<<json_quote(r.action)<<",\"root\":"<<json_quote(r.root)
       <<",\"profile\":"<<json_quote(r.profile)<<",\"files_scanned\":"<<r.files_scanned
       <<",\"files_skipped\":"<<r.files_skipped<<",\"findings_capped\":"<<r.findings_capped<<",\"summary\":{\"by_rule\":{";
    {
        bool first=true;
        for (auto& [rule,t]:tally_by_rule(r)) {
            if(!first)out<<',';first=false;
            out<<json_quote(rule)<<":{\"production_p2\":"<<t.production_p2<<",\"production_p3\":"<<t.production_p3<<",\"test\":"<<t.test<<'}';
        }
        out<<"},\"hotspots\":[";
        const auto hot=hotspots(r);
        for (std::size_t i=0;i<hot.size();++i) {if(i)out<<',';out<<"{\"path\":"<<json_quote(hot[i].first)<<",\"p2\":"<<hot[i].second<<'}';}
        out<<"]},\"gates\":[";
    }
    for (std::size_t i=0;i<r.gates.size();++i) {
        if (i) out<<',';
        const auto& g=r.gates[i];
        out<<"{\"name\":"<<json_quote(g.name)<<",\"status\":"<<json_quote(status_text(g.status))
           <<",\"details\":"<<json_quote(g.details)<<",\"required\":"<<(g.required?"true":"false")<<'}';
    }
    out<<"],\"findings\":[";
    for (std::size_t i=0;i<r.findings.size();++i) {
        if (i) out<<',';
        const auto& f=r.findings[i];
        out<<"{\"id\":"<<json_quote(f.id)<<",\"rule\":"<<json_quote(f.rule)
           <<",\"severity\":"<<json_quote(f.severity)<<",\"confidence\":"<<json_quote(f.confidence)
           <<",\"path\":"<<json_quote(f.path)<<",\"line\":"<<f.line
           <<",\"message\":"<<json_quote(f.message)<<",\"evidence\":"<<json_quote(f.evidence)
           <<",\"recommendation\":"<<json_quote(f.recommendation)
           <<",\"scope\":"<<json_quote(f.scope)
           <<",\"analyzer\":"<<json_quote(f.analyzer)<<",\"evidence_level\":"<<json_quote(f.evidence_level)
           <<",\"existing\":"<<(f.existing?"true":"false")
           <<",\"suppressed\":"<<(f.suppressed?"true":"false")<<'}';
    }
    out<<"],\"metrics\":"<<metrics_json(r)<<",\"baseline_delta\":"<<delta_json(r)<<",\"analyses\":{";
    for(std::size_t i=0;i<r.sections.size();++i){if(i)out<<',';out<<json_quote(r.sections[i].name)<<':'<<r.sections[i].json;}
    out<<"},\"notes\":[";
    for (std::size_t i=0;i<r.notes.size();++i){if(i)out<<',';out<<json_quote(r.notes[i]);}
    out<<"]}\n";return out.str();
}

// SARIF 2.1.0 for GitHub code scanning / VS Code SARIF viewer. Heuristic hints map to
// "warning" (P2) or "note" (P3); gates are attached as run properties, never as results.
std::string sarif(const Report& r) {
    std::map<std::string,const Finding*> rule_examples;
    for (auto& f:r.findings) rule_examples.emplace(f.rule,&f);
    std::ostringstream out;
    out<<"{\"$schema\":\"https://json.schemastore.org/sarif-2.1.0.json\",\"version\":\"2.1.0\",\"runs\":[{"
       <<"\"tool\":{\"driver\":{\"name\":\"cpp-rails\",\"version\":\"" CPP_RAILS_VERSION "\",\"rules\":[";
    bool first=true;
    for (auto& [rule,f]:rule_examples) {
        if(!first)out<<',';first=false;
        out<<"{\"id\":"<<json_quote(rule)<<",\"shortDescription\":{\"text\":"<<json_quote(f->message)
           <<"},\"help\":{\"text\":"<<json_quote(f->recommendation)<<"}}";
    }
    std::string root_uri=utf8_bytes(path_from_utf8(r.root).generic_u8string());
    if (root_uri.empty() || root_uri.back()!='/') root_uri.push_back('/');
    if (root_uri.front()!='/') root_uri="/"+root_uri;
    out<<"]}},\"originalUriBaseIds\":{\"SRCROOT\":{\"uri\":"<<json_quote("file://"+uri_escape(root_uri))<<"}},\"results\":[";
    first=true;
    for (auto& f:r.findings) {
        if(!first)out<<',';first=false;
        out<<"{\"ruleId\":"<<json_quote(f.rule)<<",\"level\":"<<json_quote(f.severity=="P2"?"warning":"note")
           <<",\"message\":{\"text\":"<<json_quote(f.message+". "+f.recommendation)<<"}"
           <<",\"locations\":[{\"physicalLocation\":{\"artifactLocation\":{\"uri\":"<<json_quote(uri_escape(f.path))
           <<",\"uriBaseId\":\"SRCROOT\"},\"region\":{\"startLine\":"<<std::max<std::size_t>(f.line,1)<<"}}}]"
           <<",\"partialFingerprints\":{\"cppRailsId/v1\":"<<json_quote(f.id)<<"}"
           <<",\"properties\":{\"priority\":"<<json_quote(f.severity)<<",\"scope\":"<<json_quote(f.scope)
           <<",\"confidence\":"<<json_quote(f.confidence)<<",\"analyzer\":"<<json_quote(f.analyzer)<<",\"evidence_level\":"<<json_quote(f.evidence_level)
           <<",\"baseline\":"<<(f.existing?"true":"false")<<'}';
        if (f.suppressed) out<<",\"suppressions\":[{\"kind\":\"external\",\"justification\":\"reviewed exception\"}]";
        out<<'}';
    }
    out<<"],\"properties\":{\"profile\":"<<json_quote(r.profile)<<",\"gates\":[";
    for (std::size_t i=0;i<r.gates.size();++i) {
        if(i)out<<',';
        out<<"{\"name\":"<<json_quote(r.gates[i].name)<<",\"status\":"<<json_quote(status_text(r.gates[i].status))
           <<",\"required\":"<<(r.gates[i].required?"true":"false")<<'}';
    }
    out<<"],\"metrics\":"<<metrics_json(r)<<",\"baseline_delta\":"<<delta_json(r)<<",\"analyses\":{";
    for(std::size_t i=0;i<r.sections.size();++i){if(i)out<<',';out<<json_quote(r.sections[i].name)<<':'<<r.sections[i].json;}
    out<<"}}}]}\n";
    return out.str();
}


}
