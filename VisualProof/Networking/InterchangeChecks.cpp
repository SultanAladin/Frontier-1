//============================================================================================================================================
//                                                    INTERCHANGECHECKS.CPP
//============================================================================================================================================
// 📦 Loads the real EOS-linked project image and verifies its ABI and credential refusal without authenticating.

#include <ProjectInterchange.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount != 2)
        return 2;
    unsetenv("EOS_CLIENT_SECRET");
    void* Image = dlopen(Arguments[1], RTLD_NOW | RTLD_LOCAL);
    if (!Image)
    {
        std::puts(dlerror());
        return 2;
    }
    auto Construct = reinterpret_cast<FrontierConstructProjectInterchange>(
        dlsym(Image, "ConstructProjectInterchange"));
    if (!Construct)
    {
        dlclose(Image);
        return 2;
    }
    int Failures = 0;
    const auto Require = [&Failures](bool Accepted, const char* Text)
    {
        std::printf("%s %s\n", Accepted ? "PASS" : "FAIL", Text);
        if (!Accepted)
            ++Failures;
    };
    FrontierProjectInterchange Delivery{};
    FrontierProjectRefusal Refusal{};
    Require(!Construct(0, FrontierCodeInterchangeFingerprint, &Delivery, &Refusal), "reject wrong ABI revision");
    Require(!Construct(FrontierCodeInterchangeNumber, 0, &Delivery, &Refusal), "reject wrong ABI fingerprint");
    Require(!Construct(FrontierCodeInterchangeNumber, FrontierCodeInterchangeFingerprint, nullptr, &Refusal),
        "reject null delivery");
    const bool Valid = Construct(FrontierCodeInterchangeNumber, FrontierCodeInterchangeFingerprint, &Delivery, &Refusal);
    Require(Valid && Delivery.ConstructProject && Delivery.AdvanceProject && Delivery.RetireProject,
        "load real EOS-linked project entry points");
    if (Valid && Delivery.ConstructProject && Delivery.AdvanceProject && Delivery.RetireProject)
    {
        FrontierProjectLaunch Launch{};
        Launch.StructureSize = sizeof(Launch);
        FrontierProjectHostInterchange Host{};
        Host.StructureSize = sizeof(Host);
        void* Record = nullptr;
        Require(!Delivery.ConstructProject(nullptr, &Host, &Record, &Refusal), "reject malformed launch");
        Require(!Delivery.ConstructProject(&Launch, &Host, &Record, &Refusal) && !Record,
            "missing secret refuses real project construction");
        Require(!Delivery.ConstructProject(&Launch, &Host, &Record, &Refusal), "repeat refused construction safely");
        Require(!Delivery.AdvanceProject(nullptr, nullptr, &Refusal), "reject inactive project tick");
        Delivery.RetireProject(nullptr);
    }
    dlclose(Image);
    std::puts("scope=real_library_loading_and_refusal authentication=NOT_ATTEMPTED");
    return Failures ? 1 : 0;
}
