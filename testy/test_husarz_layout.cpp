#include "../programy/husarz/paint.h"
#include "../programy/husarz/script.h"
#include <algorithm>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
using namespace Husarz;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
static Metrics metrics;
static uint16_t find(Document &d,const char *id) { auto n=ScriptDom(d).get_element_by_id(id).id; CHECK(n); return n; }
static void load(Document &d,const std::string &html,int width=320) {
    auto source=std::string("<style>body{margin:0}table{border-spacing:0}td,th{padding:0;vertical-align:top}</style>")+html;
    parse_html(d,source.data(),source.size()); collect_styles(d); prepare_controls(d); reflow(d,width,metrics);
}
static void grid_invariants(const Document &d) {
    CHECK(d.table_count<=MaxTables); CHECK(d.table_cells<=MaxTableCells); CHECK(d.table_work<=MaxTableWork);
    for (unsigned t=0;t<d.table_count;++t) {
        const auto &p=d.tables[t]; CHECK(p.rows<=MaxTableRows); CHECK(p.columns<=MaxTableColumns);
        for (unsigned r=0;r<p.rows;++r) for (unsigned c=0;c<p.columns;++c) {
            auto id=p.grid[r*MaxTableColumns+c]; if (!id) continue;
            CHECK(id<d.node_count); const auto &cell=d.table_cell[id];
            CHECK(cell.table==p.node); CHECK(cell.row<=r && cell.row+cell.rows>r);
            CHECK(cell.column<=c && cell.column+cell.columns>c);
        }
    }
    for (uint16_t n=1;n<d.node_count;++n) {
        const auto &c=d.table_cell[n]; if (!c.table) continue;
        const auto *p=table_plan(d,n); CHECK(p); CHECK(c.rows && c.rows<=MaxTableSpan); CHECK(c.columns && c.columns<=MaxTableSpan);
        for (unsigned r=c.row;r<c.row+c.rows;++r) for (unsigned col=c.column;col<c.column+c.columns;++col)
            CHECK(p->grid[r*MaxTableColumns+col]==n);
    }
}
static int text_y(Document &d,uint16_t id) {
    for (uint32_t f=0;f<d.fragment_count;++f) if (d.nodes[d.fragments[f].node].parent==id) return d.fragments[f].y;
    CHECK(false); return 0;
}
static std::vector<uint32_t> render(Document &d) {
    std::vector<uint32_t> out(320*240); Surface s{out.data(),320,240}; paint(d,s,0,metrics,{}); CHECK(!s.truncated); return out;
}
int main() {
    auto owned=std::make_unique<Document>(); auto &d=*owned;
    load(d,"<table><tr><td id=a rowspan=2>A<br>B<br>C</td><td id=b>B</td></tr><tr><td id=c>C</td></tr></table><div id=after>After</div>");
    auto a=find(d,"a"),b=find(d,"b"),c=find(d,"c");
    CHECK(d.table_cell[c].column==1); CHECK(d.boxes[c].x==d.boxes[b].x);
    CHECK(d.boxes[c].y>d.boxes[b].y); CHECK(d.boxes[a].height==d.boxes[b].height+d.boxes[c].height);
    CHECK(d.boxes[find(d,"after")].y>=d.boxes[a].y+d.boxes[a].height); grid_invariants(d);
    load(d,"<table style='width:300px'><tr><td id=a rowspan=2 colspan=2>A<br>B<br>C</td><td id=b>B</td></tr><tr><td id=c>C</td></tr><tr><td id=z colspan=3>Z</td></tr></table>");
    a=find(d,"a"); b=find(d,"b"); c=find(d,"c");
    CHECK(d.table_cell[c].column==2); CHECK(d.boxes[c].x==d.boxes[b].x);
    CHECK(d.boxes[find(d,"z")].width==300); CHECK(d.boxes[a].width+d.boxes[b].width==300); grid_invariants(d);
    load(d,"<table id=t style='width:100%'><tr><td id=a>Long <span>nested</span> text</td><td id=b>X</td></tr></table>");
    a=find(d,"a");b=find(d,"b"); CHECK(d.boxes[a].width>d.boxes[b].width);
    auto generation=d.generation, parsed=d.parse_count,arena=d.arena_used;
    CHECK(d.boxes[find(d,"t")].width==320); reflow(d,200,metrics); CHECK(d.boxes[find(d,"t")].width==200);
    CHECK(d.generation==generation && d.parse_count==parsed && d.arena_used==arena); grid_invariants(d);
    load(d,"<table style='width:300px'><tr><td style='width:70%' id=a>A</td><td id=b>B</td></tr></table>");
    CHECK(d.boxes[find(d,"a")].width>=210); CHECK(d.boxes[find(d,"b")].width<=90);
    load(d,"<table style='width:300px'><colgroup><col style='width:200px'><col></colgroup><tr><td id=a>A</td><td id=b>B</td></tr></table>");
    CHECK(d.boxes[find(d,"a")].width>=200); CHECK(d.boxes[find(d,"a")].width>d.boxes[find(d,"b")].width);
    load(d,"<table style='width:200px;border-spacing:5px'><tr><td id=a>A</td><td id=b>B</td></tr><tr><td id=c>C</td><td>D</td></tr></table>");
    a=find(d,"a");b=find(d,"b");c=find(d,"c");
    CHECK(d.boxes[a].x==5); CHECK(d.boxes[b].x==d.boxes[a].x+d.boxes[a].width+5);
    CHECK(d.boxes[c].y==d.boxes[a].y+d.boxes[a].height+5);
    CHECK(d.boxes[b].x+d.boxes[b].width+5==200);
    for (auto alignment : {"top","middle","bottom"}) {
        load(d,std::string("<table><tr><td id=a style='vertical-align:")+alignment+"'>X</td><td>A<br>B<br>C</td></tr></table>");
        a=find(d,"a"); int dy=text_y(d,a)-d.boxes[a].y;
        CHECK(d.boxes[a].height==60); CHECK(dy==(equal(alignment,"top")?4:equal(alignment,"middle")?24:44));
    }
    load(d,"<table style='width:100px;border-collapse:collapse'><tr><td id=a style='border:2px solid red'>A</td><td id=b style='border:6px solid blue'>B</td></tr></table>");
    a=find(d,"a");b=find(d,"b"); CHECK(d.boxes[b].x==d.boxes[a].x+d.boxes[a].width);
    auto out=render(d); int x=d.boxes[b].x,y=d.boxes[b].y+10;
    CHECK(out[y*320+x]==0xff0000ff); CHECK(out[y*320+x-2]==0xff0000ff); CHECK(out[y*320+x+2]==0xff0000ff);
    load(d,"<table><tbody><tr><td id=a rowspan=99>A</td></tr></tbody><tfoot><tr><td id=b>B</td></tr></tfoot></table>");
    CHECK(d.table_cell[find(d,"a")].rows==1); CHECK(d.table_cell[find(d,"b")].column==0); grid_invariants(d);
    load(d,"<table><tr><td>A</td><td rowspan=2 id=a>B</td></tr><tr><td colspan=2 id=bad>Overlap</td><td id=c>C</td></tr></table>");
    CHECK(d.truncated); CHECK(!d.table_cell[find(d,"bad")].table); CHECK(d.boxes[find(d,"bad")].height==0); grid_invariants(d);
    // Fuzz-style span grammar and deterministic grid verification.
    uint32_t random=1234567;
    const char *spans[]={"0","-1","abc","1","2","3","32","999999999999999999999999","2x"};
    for (unsigned sample=0;sample<120;++sample) {
        std::string html="<table style='width:100%'>";
        for (unsigned row=0;row<8;++row) {
            html+="<tr>";
            for (unsigned cell=0;cell<5;++cell) {
                random=random*1664525U+1013904223U;
                html+=std::string("<td rowspan='")+spans[random%9]+"' colspan='"+spans[(random/9)%9]+"'>ą<span>ł</span>x</td>";
            }
            html+="</tr>";
        }
        html+="</table>"; load(d,html,80+int(sample)); grid_invariants(d);
        auto height=d.height; auto before=render(d); reflow(d,80+int(sample),metrics);
        CHECK(height==d.height); CHECK(before==render(d)); grid_invariants(d);
    }
    std::string many="<table>"; for(unsigned r=0;r<300;++r) many+="<tr><td>x</td></tr>"; many+="</table>";
    load(d,many); CHECK(d.truncated); CHECK(d.tables[0].rows==MaxTableRows); grid_invariants(d);
    many=""; for(unsigned t=0;t<MaxTables+1;++t) many+="<table><tr><td>x</td></tr></table>";
    load(d,many); CHECK(d.truncated); CHECK(d.table_count==MaxTables); grid_invariants(d);
    many="<table>";
    for(unsigned r=0;r<35;++r) { many+="<tr>"; for(unsigned col=0;col<32;++col) many+="<td></td>"; many+="</tr>"; }
    many+="</table>"; load(d,many); CHECK(d.truncated); CHECK(d.table_cells==MaxTableCells); grid_invariants(d);
    load(d,"<table><tr><td rowspan=2>A<table><tr><td id=nested>N</td></tr></table></td><td id=b>B</td></tr><tr><td>C</td></tr></table>");
    CHECK(d.table_count==2); grid_invariants(d); CHECK(d.boxes[find(d,"nested")].height>0);
    load(d,"<table style='border-spacing:3px'><tbody id=g><tr><td rowspan=2>A<br>B<br>C</td><td id=b>B</td></tr><tr><td id=c>C</td></tr></tbody></table>");
    CHECK(d.styles[find(d,"g")].display==Display::RowGroup);
    CHECK(d.boxes[find(d,"g")].y==d.boxes[find(d,"b")].y);
    CHECK(d.boxes[find(d,"g")].height==d.boxes[find(d,"c")].y+d.boxes[find(d,"c")].height-d.boxes[find(d,"b")].y);
    // Margin struts keep positive and negative extrema independently.
    for (auto margins : {std::pair<int,int>{20,30},{-20,-30},{20,-30},{-20,30}}) {
        load(d,"<div id=a style='height:10px;margin-bottom:"+std::to_string(margins.first)+
            "px'></div><div id=b style='height:10px;margin-top:"+std::to_string(margins.second)+"px'></div>");
        int positive=std::max(0,std::max(margins.first,margins.second));
        int negative=std::min(0,std::min(margins.first,margins.second));
        a=find(d,"a");b=find(d,"b"); CHECK(d.boxes[b].y-d.boxes[a].y-d.boxes[a].height==positive+negative);
    }
    load(d,"<div id=p style='margin-top:10px'><div id=a style='margin-top:30px;height:10px;margin-bottom:20px'></div></div><div id=b style='height:10px;margin-top:5px'></div>");
    a=find(d,"a");b=find(d,"b"); auto parent=find(d,"p");
    CHECK(d.boxes[parent].y==30); CHECK(d.boxes[a].y==d.boxes[parent].y);
    CHECK(d.boxes[parent].height==10); CHECK(d.boxes[b].y==60);
    load(d,"<div id=a style='height:10px;margin-bottom:10px'></div><div id=e style='margin-top:50px;margin-bottom:-25px'></div><div id=b style='height:10px;margin-top:5px'></div>");
    a=find(d,"a");b=find(d,"b"); CHECK(d.boxes[b].y-d.boxes[a].y-d.boxes[a].height==25);
    CHECK(d.boxes[find(d,"e")].height==0);
    for (auto barrier : {"border-top-width:1px;border-style:solid","padding-top:1px","overflow:hidden"}) {
        load(d,std::string("<div id=p style='")+barrier+";margin-top:10px'><div id=a style='height:10px;margin-top:30px'></div></div>");
        a=find(d,"a");parent=find(d,"p"); CHECK(d.boxes[parent].y==10);
        CHECK(d.boxes[a].y>=d.boxes[parent].content_y+30);
    }
    load(d,"<div id=p style='margin-top:10px'><div id=q style='margin-top:20px'><div id=a style='height:10px;margin-top:-5px'></div></div></div>");
    CHECK(d.boxes[find(d,"p")].y==15); CHECK(d.boxes[find(d,"q")].y==15); CHECK(d.boxes[find(d,"a")].y==15);
    load(d,"<div id=p style='border-bottom-width:1px;border-style:solid'><div id=a style='height:10px;margin-bottom:30px'></div></div>");
    CHECK(d.boxes[find(d,"p")].height==41);
    load(d,"<div id=p>inline<div id=a style='height:10px;margin-top:30px'></div></div>");
    CHECK(d.boxes[find(d,"a")].y>=d.boxes[find(d,"p")].y+50);
    std::printf("Husarz layout: %u checks PASS; bounded logical table grid, spans, sizing, alignment, borders, resize\n",checks);
}
