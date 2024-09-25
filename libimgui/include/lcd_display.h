#include "imgui_internal.h"
#include <stdint.h>

uint32_t lcd_fg = IM_COL32(255,0,0,255);
uint32_t lcd_bg = IM_COL32(0,0,0,255);

#define W 8
#define H 4

#define draw_poly(n,i)d->AddConvexPolyFilled(pp,6,(kd[n]>>(6-i))&1 ? lcd_fg : lcd_bg)

char kd[]={0x7E,0x30,0x6D,0x79,0x33,0x5B,0x5F,0x70,0x7F,0x7B};
static void digit(ImDrawList*d,int n,ImVec2 e,ImVec2 p)
{
    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;
    ImVec4 bg = colors[ImGuiCol_WindowBg];
    bg.x += 0.12;bg.y += 0.12;bg.z += 0.12;
    lcd_bg = ImGui::ColorConvertFloat4ToU32(bg);
    float r[7][4]={{-1,-1,H,H},{1,-1,-H,H},{1,0,-H,-H},{-1,1,H,-W*1.5},{-1,0,H,-H},{-1,-1,H,H},{-1,0,H,-H},};
    for(int i=0;i<7;i++){
        ImVec2 a,b;
        if(i%3==0){
            a=ImVec2(p.x+r[i][0]*e.x+r[i][2],p.y+r[i][1]*e.y+r[i][3]-H);
            b=ImVec2(a.x+e.x*2-W,a.y+W);
        }else{
            a=ImVec2(p.x+r[i][0]*e.x+r[i][2]-H,p.y+r[i][1]*e.y+r[i][3]);
            b=ImVec2(a.x+W,a.y+e.y-W);
        }
        ImVec2 q = ImVec2(b.x-a.x, b.y-a.y);
        float s=W*0.6,u=s-H;
        if(q.x>q.y){
            ImVec2 pp[]={{a.x+u,a.y+q.y*.5f},{a.x+s,a.y},{b.x-s,a.y},{b.x-u,a.y+q.y*.5f},{b.x-s,b.y},{a.x+s,b.y}};
            draw_poly(n,i);
        }else{
            ImVec2 pp[]={{a.x+q.x*.5f,a.y+u},{b.x,a.y+s},{b.x,b.y-s},{b.x-q.x*.5f,b.y-u},{a.x,b.y-s},{a.x,a.y+s}};
            draw_poly(n,i);
        }
    }
}
#undef W
#undef H
#undef v
#undef draw_poly