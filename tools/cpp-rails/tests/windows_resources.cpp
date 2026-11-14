// Executable counterexamples: the user-data slot borrows unless the design
// explicitly delegates destruction. COM acquires cleanup only on success.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <type_traits>
static void require(bool ok) {if(!ok)throw std::runtime_error("resource contract failed");}
struct WindowState {
    int& destructions;bool self_owned=false;HWND hwnd=nullptr;
    explicit WindowState(int& count,bool owned=false):destructions(count),self_owned(owned){}
    WindowState(const WindowState&)=delete;WindowState& operator=(const WindowState&)=delete;
    ~WindowState(){if(hwnd)DestroyWindow(hwnd);++destructions;}
};
static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        state=static_cast<WindowState*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        state->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    if(message==WM_NCDESTROY&&state) {
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);state->hwnd=nullptr;
        if(state->self_owned)delete state;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
class Apartment {
    int& cleanups_;
public:
    const HRESULT result;
    Apartment(DWORD mode,int& cleanups):cleanups_(cleanups),result(CoInitializeEx(nullptr,mode)){}
    ~Apartment(){if(SUCCEEDED(result)){CoUninitialize();++cleanups_;}}
    Apartment(const Apartment&)=delete;Apartment& operator=(const Apartment&)=delete;
    Apartment(Apartment&&)=delete;Apartment& operator=(Apartment&&)=delete; // thread-affine
};
static_assert(!std::is_copy_constructible_v<Apartment> && !std::is_move_constructible_v<Apartment>);
int main() {
    try {
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"CppRailsResourceFixture";
        require(RegisterClassW(&cls)!=0);
        auto create=[&](WindowState& state) {auto hwnd=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,&state);require(hwnd!=nullptr);return hwnd;};
        int count=0;
        {WindowState automatic{count};require(DestroyWindow(create(automatic))!=0);require(count==0&&automatic.hwnd==nullptr);}
        require(count==1);
        {auto external=std::make_unique<WindowState>(count);const int before=count;require(DestroyWindow(create(*external))!=0);require(count==before&&external->hwnd==nullptr);}
        const int before=count;auto* owned=new WindowState{count,true};require(DestroyWindow(create(*owned))!=0);require(count==before+1);
        UnregisterClassW(cls.lpszClassName,cls.hInstance);
        int cleanups=0;
        {Apartment first(COINIT_APARTMENTTHREADED,cleanups);require(first.result==S_OK);
            {Apartment again(COINIT_APARTMENTTHREADED,cleanups);require(again.result==S_FALSE);}require(cleanups==1);
            {Apartment incompatible(COINIT_MULTITHREADED,cleanups);require(incompatible.result==RPC_E_CHANGED_MODE);}require(cleanups==1);
            {Apartment still_initialized(COINIT_APARTMENTTHREADED,cleanups);require(still_initialized.result==S_FALSE);}require(cleanups==2);
        }
        require(cleanups==3);
        std::cout<<"Window stack/external/self-owned lifetimes and COM S_OK/S_FALSE/RPC_E_CHANGED_MODE PASS\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
