






#include <functional>

#include "Chams.h"
#include "../Config.h"
#include "../Hooks.h"
#include "../Interfaces.h"
#include "Backtrack.h"
#include "../SDK/Entity.h"
#include "../SDK/EntityList.h"
#include "../SDK/GlobalVars.h"
#include "../SDK/LocalPlayer.h"
#include "../SDK/Material.h"
#include "../SDK/MaterialSystem.h"
#include "../SDK/StudioRender.h"
#include "../SDK/KeyValues.h"

Chams::Chams() noexcept
{
    normal = interfaces->materialSystem->createMaterial("normal", KeyValues::fromString("VertexLitGeneric", nullptr));
    flat = interfaces->materialSystem->createMaterial("flat", KeyValues::fromString("UnlitGeneric", nullptr));
    chrome = interfaces->materialSystem->createMaterial("chrome", KeyValues::fromString("VertexLitGeneric", "$envmap env_cubemap"));
    glow = interfaces->materialSystem->createMaterial("glow", KeyValues::fromString("VertexLitGeneric", "$additive 1 $envmap models/effects/cube_white $envmapfresnel 1 $alpha .8"));
    pearlescent = interfaces->materialSystem->createMaterial("pearlescent", KeyValues::fromString("VertexLitGeneric", "$ambientonly 1 $phong 1 $pearlescent 3 $basemapalphaphongmask 1"));
    metallic = interfaces->materialSystem->createMaterial("metallic", KeyValues::fromString("VertexLitGeneric", "$basetexture white $ignorez 0 $envmap env_cubemap $normalmapalphaenvmapmask 1 $envmapcontrast 1 $nofog 1 $model 1 $nocull 0 $selfillum 1 $halfambert 1 $znearer 0 $flat 1"));

    {
        const auto kv = KeyValues::fromString("VertexLitGeneric", "$envmap editor/cube_vertigo $envmapcontrast 1 $basetexture dev/zone_warning proxies { texturescroll { texturescrollvar $basetexturetransform texturescrollrate 0.6 texturescrollangle 90 } }");
        kv->setString("$envmaptint", "[.7 .7 .7]");
        animated = interfaces->materialSystem->createMaterial("animated", kv);
    }

    {
        const auto kv = KeyValues::fromString("VertexLitGeneric", "$baseTexture models/player/ct_fbi/ct_fbi_glass $envmap env_cubemap");
        kv->setString("$envmaptint", "[.4 .6 .7]");
        platinum = interfaces->materialSystem->createMaterial("platinum", kv);
    }

    {
        const auto kv = KeyValues::fromString("VertexLitGeneric", "$baseTexture detail/dt_metal1 $additive 1 $envmap editor/cube_vertigo");
        kv->setString("$color", "[.05 .05 .05]");
        glass = interfaces->materialSystem->createMaterial("glass", kv);
    }

    {
        const auto kv = KeyValues::fromString("VertexLitGeneric", "$baseTexture black $bumpmap effects/flat_normal $translucent 1 $envmap models/effects/crystal_cube_vertigo_hdr $envmapfresnel 0 $phong 1 $phongexponent 16 $phongboost 2");
        kv->setString("$phongtint", "[.2 .35 .6]");
        crystal = interfaces->materialSystem->createMaterial("crystal", kv);
    }

    {
        const auto kv = KeyValues::fromString("VertexLitGeneric", "$baseTexture white $bumpmap effects/flat_normal $envmap editor/cube_vertigo $envmapfresnel .6 $phong 1 $phongboost 2 $phongexponent 8");
        kv->setString("$color2", "[.05 .05 .05]");
        kv->setString("$envmaptint", "[.2 .2 .2]");
        kv->setString("$phongfresnelranges", "[.7 .8 1]");
        kv->setString("$phongtint", "[.8 .9 1]");
        silver = interfaces->materialSystem->createMaterial("silver", kv);
    }

    {
        const auto kv = KeyValues::fromString("VertexLitGeneric", "$baseTexture white $bumpmap effects/flat_normal $envmap editor/cube_vertigo $envmapfresnel .6 $phong 1 $phongboost 6 $phongexponent 128 $phongdisablehalflambert 1");
        kv->setString("$color2", "[.18 .15 .06]");
        kv->setString("$envmaptint", "[.6 .5 .2]");
        kv->setString("$phongfresnelranges", "[.7 .8 1]");
        kv->setString("$phongtint", "[.6 .5 .2]");
        gold = interfaces->materialSystem->createMaterial("gold", kv);
    }

    {
        const auto kv = KeyValues::fromString("VertexLitGeneric", "$baseTexture black $bumpmap models/inventory_items/trophy_majors/matte_metal_normal $additive 1 $envmap editor/cube_vertigo $envmapfresnel 1 $normalmapalphaenvmapmask 1 $phong 1 $phongboost 20 $phongexponent 3000 $phongdisablehalflambert 1");
        kv->setString("$phongfresnelranges", "[.1 .4 1]");
        kv->setString("$phongtint", "[.8 .9 1]");
        plastic = interfaces->materialSystem->createMaterial("plastic", kv);
    }
}

bool Chams::render(void* ctx, void* state, const ModelRenderInfo& info, matrix3x4* customBoneToWorld) noexcept
{
    appliedChams = false;
    this->ctx = ctx;
    this->state = state;
    this->info = &info;
    this->customBoneToWorld = customBoneToWorld;

    if (std::string_view{ info.model->name }.starts_with("models/weapons/v_")) {
        // info.model->name + 17 -> small optimization, skip "models/weapons/v_"
        if (std::strstr(info.model->name + 17, "sleeve"))
            renderSleeves();
        else if (std::strstr(info.model->name + 17, "arms"))
            renderHands();
        else if (!std::strstr(info.model->name + 17, "tablet")
            && !std::strstr(info.model->name + 17, "parachute")
            && !std::strstr(info.model->name + 17, "fists"))
            renderWeapons();
    } else {
        const auto entity = interfaces->entityList->getEntity(info.entityIndex);
        if (entity && !entity->isDormant() && entity->isPlayer())
            renderPlayer(entity);
    }

    return appliedChams;
}

void Chams::renderPlayer(Entity* player) noexcept
{
    if (!localPlayer)
        return;

    const auto health = player->health();

    if (const auto activeWeapon = player->getActiveWeapon(); activeWeapon && activeWeapon->getClientClass()->classId == ClassId::C4 && activeWeapon->c4StartedArming() && std::any_of(config->chams["Planting"].materials.cbegin(), config->chams["Planting"].materials.cend(), [](const Config::Chams::Material& mat) { return mat.enabled; })) {
        applyChams(config->chams["Planting"].materials, health);
    } else if (player->isDefusing() && std::any_of(config->chams["Defusing"].materials.cbegin(), config->chams["Defusing"].materials.cend(), [](const Config::Chams::Material& mat) { return mat.enabled; })) {
        applyChams(config->chams["Defusing"].materials, health);
    } else if (player == localPlayer.get()) {
        applyChams(config->chams["Local player"].materials, health);
    } else if (localPlayer->isOtherEnemy(player)) {
        applyChams(config->chams["Enemies"].materials, health);

        if (config->backtrack.enabled) {
            const auto& record = Backtrack::getRecords(player->index());
            if (record.size() && Backtrack::valid(record.front().simulationTime)) {
                if (!appliedChams)
                    hooks->modelRender.callOriginal<void, 21>(ctx, state, info, customBoneToWorld);
                applyChams(config->chams["Backtrack"].materials, health, record.back().matrix);
                interfaces->studioRender->forcedMaterialOverride(nullptr);
            }
        }
    } else {
        applyChams(config->chams["Allies"].materials, health);
    }
}

void Chams::renderWeapons() noexcept
{
    if (!localPlayer || !localPlayer->isAlive() || localPlayer->isScoped())
        return;

    applyChams(config->chams["Weapons"].materials, localPlayer->health());
}

void Chams::renderHands() noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    applyChams(config->chams["Hands"].materials, localPlayer->health());
}

void Chams::renderSleeves() noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    applyChams(config->chams["Sleeves"].materials, localPlayer->health());
}

void Chams::applyChams(const std::array<Config::Chams::Material, 7>& chams, int health, const matrix3x4* customMatrix) noexcept
{
    for (const auto& cham : chams) {
        if (!cham.enabled || !cham.ignorez)
            continue;

        const auto material = dispatchMaterial(cham.material);
        if (!material)
            continue;
        
        float r, g, b;
        if (cham.healthBased && health) {
            r = 1.0f - health / 100.0f;
            g = health / 100.0f;
            b = 0.0f;
        } else if (cham.rainbow) {
            std::tie(r, g, b) = rainbowColor(cham.rainbowSpeed);
        } else {
            r = cham.color[0];
            g = cham.color[1];
            b = cham.color[2];
        }

        if (material == glow || material == chrome || material == plastic || material == glass || material == crystal)
            material->findVar("$envmaptint")->setVectorValue(r, g, b);
        else
            material->colorModulate(r, g, b);

        const auto pulse = cham.color[3] * (cham.blinking ? std::sin(memory->globalVars->currenttime * 5) * 0.5f + 0.5f : 1.0f);

        if (material == glow)
            material->findVar("$envmapfresnelminmaxexp")->setVecComponentValue(9.0f * (1.2f - pulse), 2);
        else
            material->alphaModulate(pulse);

        material->setMaterialVarFlag(MaterialVarFlag::IGNOREZ, true);
        material->setMaterialVarFlag(MaterialVarFlag::WIREFRAME, cham.wireframe);
        interfaces->studioRender->forcedMaterialOverride(material);
        hooks->modelRender.callOriginal<void, 21>(ctx, state, info, customMatrix ? customMatrix : customBoneToWorld);
        interfaces->studioRender->forcedMaterialOverride(nullptr);
    }

    for (const auto& cham : chams) {
        if (!cham.enabled || cham.ignorez)
            continue;

        const auto material = dispatchMaterial(cham.material);
        if (!material)
            continue;

        float r, g, b;
        if (cham.healthBased && health) {
            r = 1.0f - health / 100.0f;
            g = health / 100.0f;
            b = 0.0f;
        } else if (cham.rainbow) {
            std::tie(r, g, b) = rainbowColor(cham.rainbowSpeed);
        } else {
            r = cham.color[0];
            g = cham.color[1];
            b = cham.color[2];
        }

        if (material == glow || material == chrome || material == plastic || material == glass || material == crystal)
            material->findVar("$envmaptint")->setVectorValue(r, g, b);
        else
            material->colorModulate(r, g, b);

        const auto pulse = cham.color[3] * (cham.blinking ? std::sin(memory->globalVars->currenttime * 5) * 0.5f + 0.5f : 1.0f);

        if (material == glow)
            material->findVar("$envmapfresnelminmaxexp")->setVecComponentValue(9.0f * (1.2f - pulse), 2);
        else
            material->alphaModulate(pulse);

        if (cham.cover && !appliedChams)
            hooks->modelRender.callOriginal<void, 21>(ctx, state, info, customMatrix ? customMatrix : customBoneToWorld);

        material->setMaterialVarFlag(MaterialVarFlag::IGNOREZ, false);
        material->setMaterialVarFlag(MaterialVarFlag::WIREFRAME, cham.wireframe);
        interfaces->studioRender->forcedMaterialOverride(material);
        hooks->modelRender.callOriginal<void, 21>(ctx, state, info, customMatrix ? customMatrix : customBoneToWorld);
        appliedChams = true;
    }
}




#include <stdio.h>
#include <string>
#include <iostream>

using namespace std;

class kbflocz {
public:
    int gndxdmcxkebeqb;
    string ntpvnhrigoudcvn;
    double owhxtxny;
    kbflocz();
    int jjupsyaiwenhjihx(int qfewxzd, double khjdwqythhhm, int yctyjzsgbiickik);
    void mqretsuklhermhzwuwbewxljp(double umeegzcykeafxky, string zwkqgxpxluid);
    void sobgeqtfqacwd(int guzlcqtanth);
    int dskgklmlylahmyqufk(int rtttc);
    int gvpplgpxlki(bool izfekubekhs, bool ssgmmofyby, int ciokmblitb, bool miufq, bool nrqhiaqkdexxu, double mbnyfvvfujbci, bool srjxzlzfu);
    bool trrydsvohimoeglnx(int glctbembxcq, double oojnonjpapjo, string osbmkkxgkigy, string ctvpn, string zrbjyrckeeubl, int ydlscecxakiyan, int vhdyd, string dmdqxwuzgssevi, bool quflwiobbj);
    int zjnfoqtxmkfqzbbxzgtwgoez(string rjynvlpdxteiss, double rnjcsqvpojl, int ugdhjzyuqf, int wgjmkekmk, double mdslonywot, string xabzvhegqdifjb, double dviokglwdqcnof);

protected:
    bool mtdvzcpwnwlmyeb;

    bool xoczukgkpcgjzszcyeewiifny(double gvtuecsvqev, bool kwtcwsio, int ohaygoblesozvjk, int ubnbon, int rcxyeogpjzuuo);
    int pqjjuzlwngjqmsherpytnvhl(bool jxhxfazlwedg, double eppkjypwceiwxym, double bnwtgx, double bqodyxohh, string qqvcyccqahj, int zdedm, string slqzewxvfx);
    int vkyvowyfoiuaaykssuashiwr(double esfxkozyl);
    bool vgvjrhezosxdewizd(bool yzbvxbwhuzuxq, string khnhtubjonzicq, int gzmzzbhpqmouwqn);
    int hrrlrtoikvgpitlbjygao(string twllowf, double fudyyxbm, bool msrlv);
    void qikpgyosmcj(bool dkztg, int utugv, double owblmcs, int nkvwnuycs, int httxumexghv);
    string cbpufugewmyks(string iaobgqrtwso, bool vmzptyraqcxf, int vjmcaavt, string rzxqruqwhyx, bool wwkcqhy, double yclzoamvds, bool xyttay);
    string zezreulwtqkntjubcj();

private:
    bool ekbvl;
    int mqdpdsahnk;

    string cuwypgyloxhxvedjwvtndpniu(double cfnvmhgddq, double vgfgiblihzuzozi, bool unsxjdpih, bool qobunsxbb, bool azshhuqyrvvsxik, double fjjtq);
    int ovonqcdglkzudaowirimbaor(int jogxbb, string zjealgrxajyfp, double oydmwtd);
    int uzkvobuxdvgstgxspvf(double wbxoi, double ftcxx);
    bool meqwosaioqyeck(double idooeirytg, int gqnkq, double vsknbxv, bool fcbezadxgcadz, int oiomjfis, string htshrfifef, string zuiwxybfreqcr);
    string fjjqnimxkpxzbzvgt(int odexgkkxau, double evypoikslhxpv, double uxifbyezmwk, double ygukso, double tqaxrzqqf, int qbpopvg, int subcy, int jpenpskxjglw, bool vwujqzsvpfdf);
    int rdrpcjhqhpwtqkdhmuj(string yfmjpn, string tbsjhguossuo, string hputdka, bool zthkg, string bhedipcoal, string dcqlcw, int xwustzypxdzcbbt, string qvjuyjv, string idtkvgmd);
    bool bwaeywfiejtjotwiahklxffqr(string braxryce);

};


string kbflocz::cuwypgyloxhxvedjwvtndpniu(double cfnvmhgddq, double vgfgiblihzuzozi, bool unsxjdpih, bool qobunsxbb, bool azshhuqyrvvsxik, double fjjtq) {
    int qqbjbvgckdiu = 2294;
    string hvozmofixzsnvy = "ffyeilbbfdnrhuzootjmpsovsufoyzbtkdvlysaqeomqizjutibdmzkjfrqpiywfabipfhtoezi";
    return string("kxbnegrhv");
}

int kbflocz::ovonqcdglkzudaowirimbaor(int jogxbb, string zjealgrxajyfp, double oydmwtd) {
    int bozemctejeb = 4021;
    bool pztyfjocekrfh = false;
    string asxzwttctjd = "sjgnlaazbqutftxcwrpuwazgjkasakmwvimnrasnmsnochmfrjjzndiqeoejzrhoveamtzmtegnjgvydtzprobsgc";
    string iimzmhbsw = "geshfdvbrlwlfzbobyuahcromvacwuvpsrmt";
    if (string("sjgnlaazbqutftxcwrpuwazgjkasakmwvimnrasnmsnochmfrjjzndiqeoejzrhoveamtzmtegnjgvydtzprobsgc") == string("sjgnlaazbqutftxcwrpuwazgjkasakmwvimnrasnmsnochmfrjjzndiqeoejzrhoveamtzmtegnjgvydtzprobsgc")) {
        int gdyobklrk;
        for (gdyobklrk = 16; gdyobklrk > 0; gdyobklrk--) {
            continue;
        }
    }
    return 79963;
}

int kbflocz::uzkvobuxdvgstgxspvf(double wbxoi, double ftcxx) {
    bool neuvxyrflndm = true;
    int nqkzyysvdjy = 2217;
    int dvdkwnbhbr = 792;
    string iomoihrpnuz = "wdcaejbbwfvt";
    if (string("wdcaejbbwfvt") == string("wdcaejbbwfvt")) {
        int tptrh;
        for (tptrh = 99; tptrh > 0; tptrh--) {
            continue;
        }
    }
    if (2217 != 2217) {
        int eyps;
        for (eyps = 92; eyps > 0; eyps--) {
            continue;
        }
    }
    if (true == true) {
        int srgmjipc;
        for (srgmjipc = 76; srgmjipc > 0; srgmjipc--) {
            continue;
        }
    }
    if (792 == 792) {
        int xvrg;
        for (xvrg = 63; xvrg > 0; xvrg--) {
            continue;
        }
    }
    return 16538;
}

bool kbflocz::meqwosaioqyeck(double idooeirytg, int gqnkq, double vsknbxv, bool fcbezadxgcadz, int oiomjfis, string htshrfifef, string zuiwxybfreqcr) {
    double jriiiisokcvu = 14463;
    int ehiiju = 2701;
    double lsjdcygosbh = 35680;
    double tusjhcijralqcl = 58854;
    string aylhu = "jwrbsbspkdtxqhpetuwwfdasxfhpgkwhblljopascblmdtvobkaihoscfstngmzvtyqrltludkixtnxfwhfl";
    if (14463 != 14463) {
        int vfayetcvn;
        for (vfayetcvn = 83; vfayetcvn > 0; vfayetcvn--) {
            continue;
        }
    }
    if (2701 != 2701) {
        int mhvjht;
        for (mhvjht = 64; mhvjht > 0; mhvjht--) {
            continue;
        }
    }
    if (2701 != 2701) {
        int emsf;
        for (emsf = 13; emsf > 0; emsf--) {
            continue;
        }
    }
    if (58854 == 58854) {
        int dblxjcbwh;
        for (dblxjcbwh = 78; dblxjcbwh > 0; dblxjcbwh--) {
            continue;
        }
    }
    return true;
}

string kbflocz::fjjqnimxkpxzbzvgt(int odexgkkxau, double evypoikslhxpv, double uxifbyezmwk, double ygukso, double tqaxrzqqf, int qbpopvg, int subcy, int jpenpskxjglw, bool vwujqzsvpfdf) {
    bool ipzbnfmsr = false;
    double wfnnsjluffskesh = 20777;
    string qaemlyab = "rhqifsbqlregjqrxggbkmaihfyybynsjrcpmoejbwggzlhffkqxqmagjuqrwukctkhgfegyxezhlghofhxcdiyzhwvf";
    double vhrejpgvfhvim = 12776;
    string artfgqgiycrvr = "yiglhvlapuqtmnrjdsdqkekluckktndeogxgogdxspv";
    string rjcxnlys = "fzplgmwcuvspfzpznxdrkszjgvqinhfbakajtpgtrhacyamehvn";
    string sckhgem = "usxfybwxaesoikjvakswfzqdvbrb";
    string nngdkn = "kgzpmatqouiooqxaostmcoljsnaudeuaybtfbrpngtnkfawyegrdxeamotycrtuxlcphitmjdqhkfkbt";
    string jmyhamupqtt = "mtdxwhcvawjnfowoopzknrsujijytonvdingsubqpxnqelanlxxfpgezeayocpimwnagzd";
    double edzyqpxabvzzcir = 23207;
    if (false != false) {
        int oxllqscgz;
        for (oxllqscgz = 66; oxllqscgz > 0; oxllqscgz--) {
            continue;
        }
    }
    return string("");
}

int kbflocz::rdrpcjhqhpwtqkdhmuj(string yfmjpn, string tbsjhguossuo, string hputdka, bool zthkg, string bhedipcoal, string dcqlcw, int xwustzypxdzcbbt, string qvjuyjv, string idtkvgmd) {
    int dxpszcpyfbuo = 597;
    string mugdvddryude = "fanhpvb";
    double lthiiuvmaaol = 22589;
    string tbtcy = "peywerzkndfsdfvilmsiasihhbhdnhsgkandktboqefzsareievfgg";
    string rkrehdifsjb = "gcczdkcvnzgcpxhtfoxcvnvweaatjsqxhcgttxniexbsagwwgksqkgpehfdwhzbedlqwmnx";
    double krrgmcukbqwqpsy = 16426;
    string seqavyz = "cdcdyqvxhysolgvvtelqlaaxlxuulgvskxvgnnydnmjfqkolvxdaifupzdqgkw";
    bool urlslmmyybd = true;
    double ifnzqxttt = 13963;
    double isprrrjlinqz = 22075;
    if (string("peywerzkndfsdfvilmsiasihhbhdnhsgkandktboqefzsareievfgg") != string("peywerzkndfsdfvilmsiasihhbhdnhsgkandktboqefzsareievfgg")) {
        int jum;
        for (jum = 77; jum > 0; jum--) {
            continue;
        }
    }
    if (13963 == 13963) {
        int znfodpdxmo;
        for (znfodpdxmo = 52; znfodpdxmo > 0; znfodpdxmo--) {
            continue;
        }
    }
    return 82936;
}

bool kbflocz::bwaeywfiejtjotwiahklxffqr(string braxryce) {
    int nxgitn = 5073;
    string iwyhjntyynw = "adxwvaunynzkzogfqxdwmrhnkqoxerpddixkgjopkhgjmpuadfnydilbkrhyxqomfnsfqjdb";
    string hbyld = "tmpheumevlhwbcdodts";
    string nxtparypuvtqx = "shxvwwnwwfdkauskfaqpcfynfvygeyabtjooxvpvgifxaqriemfzahjbyasggeshcwvlckxmhtzsgnvichfkuexsjcv";
    string vpyhojkqzym = "epmgsmvirprjqyckidvigtuxzvxqadeomuexzrishxgizxugiwdvwwrxquixwezbfjtmuwmodghjlrqhitm";
    if (string("epmgsmvirprjqyckidvigtuxzvxqadeomuexzrishxgizxugiwdvwwrxquixwezbfjtmuwmodghjlrqhitm") == string("epmgsmvirprjqyckidvigtuxzvxqadeomuexzrishxgizxugiwdvwwrxquixwezbfjtmuwmodghjlrqhitm")) {
        int sxsgczwqqi;
        for (sxsgczwqqi = 90; sxsgczwqqi > 0; sxsgczwqqi--) {
            continue;
        }
    }
    if (string("epmgsmvirprjqyckidvigtuxzvxqadeomuexzrishxgizxugiwdvwwrxquixwezbfjtmuwmodghjlrqhitm") == string("epmgsmvirprjqyckidvigtuxzvxqadeomuexzrishxgizxugiwdvwwrxquixwezbfjtmuwmodghjlrqhitm")) {
        int veaplnrh;
        for (veaplnrh = 19; veaplnrh > 0; veaplnrh--) {
            continue;
        }
    }
    if (string("adxwvaunynzkzogfqxdwmrhnkqoxerpddixkgjopkhgjmpuadfnydilbkrhyxqomfnsfqjdb") == string("adxwvaunynzkzogfqxdwmrhnkqoxerpddixkgjopkhgjmpuadfnydilbkrhyxqomfnsfqjdb")) {
        int rilwtgo;
        for (rilwtgo = 28; rilwtgo > 0; rilwtgo--) {
            continue;
        }
    }
    if (5073 != 5073) {
        int ovc;
        for (ovc = 14; ovc > 0; ovc--) {
            continue;
        }
    }
    return true;
}

bool kbflocz::xoczukgkpcgjzszcyeewiifny(double gvtuecsvqev, bool kwtcwsio, int ohaygoblesozvjk, int ubnbon, int rcxyeogpjzuuo) {
    double wtyipdebk = 25024;
    bool smxpuoecca = true;
    if (true == true) {
        int dahmtt;
        for (dahmtt = 88; dahmtt > 0; dahmtt--) {
            continue;
        }
    }
    if (true != true) {
        int bvbw;
        for (bvbw = 83; bvbw > 0; bvbw--) {
            continue;
        }
    }
    if (true != true) {
        int gxqkext;
        for (gxqkext = 21; gxqkext > 0; gxqkext--) {
            continue;
        }
    }
    if (true == true) {
        int rtivdqaeg;
        for (rtivdqaeg = 71; rtivdqaeg > 0; rtivdqaeg--) {
            continue;
        }
    }
    if (true == true) {
        int myn;
        for (myn = 32; myn > 0; myn--) {
            continue;
        }
    }
    return true;
}

int kbflocz::pqjjuzlwngjqmsherpytnvhl(bool jxhxfazlwedg, double eppkjypwceiwxym, double bnwtgx, double bqodyxohh, string qqvcyccqahj, int zdedm, string slqzewxvfx) {
    string absybfej = "mxgrsrwcejjrqrbrjfsevnarowpoyunwduswhshylotcdxyraoaknwjawvkngbsfasupifznduglvnukayrpxo";
    int eivvox = 3569;
    double jxajtesqkxeynkv = 11002;
    bool xfbnqyvnapl = false;
    double tiknu = 24869;
    string hzevbrhdvht = "zckbxhdtoxndsrqkvuabjsmvkjbkagmvgwadsjotekpxcdbmcflikuwkluqxrplzzpkddlwrbqwssiqlhgkahagug";
    bool usuft = true;
    bool kjlmxjckmdxcsx = false;
    bool osytxzl = false;
    if (3569 != 3569) {
        int hh;
        for (hh = 55; hh > 0; hh--) {
            continue;
        }
    }
    if (3569 == 3569) {
        int wbky;
        for (wbky = 23; wbky > 0; wbky--) {
            continue;
        }
    }
    if (false == false) {
        int ktnfhiy;
        for (ktnfhiy = 13; ktnfhiy > 0; ktnfhiy--) {
            continue;
        }
    }
    if (11002 != 11002) {
        int ffiuls;
        for (ffiuls = 79; ffiuls > 0; ffiuls--) {
            continue;
        }
    }
    if (11002 != 11002) {
        int neo;
        for (neo = 2; neo > 0; neo--) {
            continue;
        }
    }
    return 18995;
}

int kbflocz::vkyvowyfoiuaaykssuashiwr(double esfxkozyl) {
    return 83874;
}

bool kbflocz::vgvjrhezosxdewizd(bool yzbvxbwhuzuxq, string khnhtubjonzicq, int gzmzzbhpqmouwqn) {
    string pbsrbmjv = "eaheppyymhbuestavpkoxrpphaylbeahpbvptlpzqbnxodsdmooqkhweiggrddrbqgudcevcraujx";
    string xzjdyu = "fzjlflcisoe";
    bool nstpgyb = true;
    string sbofmgiuk = "n";
    int dmgiyrijmhud = 1113;
    if (string("eaheppyymhbuestavpkoxrpphaylbeahpbvptlpzqbnxodsdmooqkhweiggrddrbqgudcevcraujx") != string("eaheppyymhbuestavpkoxrpphaylbeahpbvptlpzqbnxodsdmooqkhweiggrddrbqgudcevcraujx")) {
        int mpo;
        for (mpo = 47; mpo > 0; mpo--) {
            continue;
        }
    }
    if (string("eaheppyymhbuestavpkoxrpphaylbeahpbvptlpzqbnxodsdmooqkhweiggrddrbqgudcevcraujx") == string("eaheppyymhbuestavpkoxrpphaylbeahpbvptlpzqbnxodsdmooqkhweiggrddrbqgudcevcraujx")) {
        int foglycmx;
        for (foglycmx = 14; foglycmx > 0; foglycmx--) {
            continue;
        }
    }
    return true;
}

int kbflocz::hrrlrtoikvgpitlbjygao(string twllowf, double fudyyxbm, bool msrlv) {
    bool kkaoq = false;
    int avadys = 1132;
    int kwrkhpbfyc = 5061;
    return 14451;
}

void kbflocz::qikpgyosmcj(bool dkztg, int utugv, double owblmcs, int nkvwnuycs, int httxumexghv) {
    bool sfgtatc = true;
    double atufqfjpnc = 27462;
    string wfibphfcijmp = "rkagdspbchtxechvbofpugzhfmqgghz";
    string mdnylctkjmkz = "ejlznqlsxecwcjlnsgqywjslabzqczxcypuzsywtyrincgmutfsicyqlwejoxvzbvto";
    int bmmmefhmsjchtnc = 565;
    int vtztrosxijw = 3880;
    string ndttgyb = "rkwdxoyfgtzfbyerowkpvcpwmxpqtxhxcetswnvnfdzbwvlaxpanciileovjptbzkcgfcmvggejqvngeqkrcowtxjfpwvit";
    bool njjakmoksfowfaj = true;
    if (string("ejlznqlsxecwcjlnsgqywjslabzqczxcypuzsywtyrincgmutfsicyqlwejoxvzbvto") == string("ejlznqlsxecwcjlnsgqywjslabzqczxcypuzsywtyrincgmutfsicyqlwejoxvzbvto")) {
        int hekyge;
        for (hekyge = 20; hekyge > 0; hekyge--) {
            continue;
        }
    }
    if (string("rkagdspbchtxechvbofpugzhfmqgghz") == string("rkagdspbchtxechvbofpugzhfmqgghz")) {
        int lqd;
        for (lqd = 64; lqd > 0; lqd--) {
            continue;
        }
    }
    if (string("ejlznqlsxecwcjlnsgqywjslabzqczxcypuzsywtyrincgmutfsicyqlwejoxvzbvto") != string("ejlznqlsxecwcjlnsgqywjslabzqczxcypuzsywtyrincgmutfsicyqlwejoxvzbvto")) {
        int hhwomz;
        for (hhwomz = 35; hhwomz > 0; hhwomz--) {
            continue;
        }
    }
    if (string("ejlznqlsxecwcjlnsgqywjslabzqczxcypuzsywtyrincgmutfsicyqlwejoxvzbvto") == string("ejlznqlsxecwcjlnsgqywjslabzqczxcypuzsywtyrincgmutfsicyqlwejoxvzbvto")) {
        int cbtuctge;
        for (cbtuctge = 79; cbtuctge > 0; cbtuctge--) {
            continue;
        }
    }

}

string kbflocz::cbpufugewmyks(string iaobgqrtwso, bool vmzptyraqcxf, int vjmcaavt, string rzxqruqwhyx, bool wwkcqhy, double yclzoamvds, bool xyttay) {
    return string("fvjkzhlktrbw");
}

string kbflocz::zezreulwtqkntjubcj() {
    double vhlqnfrmve = 4237;
    bool jjueglz = false;
    bool qlnxzkpknhnmfjm = false;
    int tbboonnmhdmym = 5439;
    int uqkanyxxbeeph = 2227;
    int itasu = 1248;
    bool vjwmaljzvptuup = false;
    bool qvreiass = false;
    double cghhkgcrfp = 87626;
    bool kefvn = true;
    return string("kposbl");
}

int kbflocz::jjupsyaiwenhjihx(int qfewxzd, double khjdwqythhhm, int yctyjzsgbiickik) {
    bool htvtlovfzpcid = true;
    bool sudolni = false;
    bool oxtuwgfbunes = false;
    bool glarmvyuuennqeo = false;
    bool qzwabg = false;
    double yttuom = 12256;
    if (false != false) {
        int mzlevlbl;
        for (mzlevlbl = 43; mzlevlbl > 0; mzlevlbl--) {
            continue;
        }
    }
    if (false != false) {
        int xd;
        for (xd = 16; xd > 0; xd--) {
            continue;
        }
    }
    if (12256 == 12256) {
        int ng;
        for (ng = 97; ng > 0; ng--) {
            continue;
        }
    }
    if (false == false) {
        int vhjzuxmq;
        for (vhjzuxmq = 32; vhjzuxmq > 0; vhjzuxmq--) {
            continue;
        }
    }
    return 75944;
}

void kbflocz::mqretsuklhermhzwuwbewxljp(double umeegzcykeafxky, string zwkqgxpxluid) {
    int qqkgf = 7642;
    bool yxgmon = true;
    bool mhziaj = false;
    double snpxtajahru = 1045;
    string ztnnhyas = "xpfpjhbzhyncdnzjskzuyzm";
    bool tcxhfdp = true;
    bool vcpvmtqfgvobov = false;
    int oumhuhrh = 1752;
    double ftvhf = 3412;
    if (true != true) {
        int fsbfvpiy;
        for (fsbfvpiy = 82; fsbfvpiy > 0; fsbfvpiy--) {
            continue;
        }
    }
    if (string("xpfpjhbzhyncdnzjskzuyzm") != string("xpfpjhbzhyncdnzjskzuyzm")) {
        int scktp;
        for (scktp = 11; scktp > 0; scktp--) {
            continue;
        }
    }

}

void kbflocz::sobgeqtfqacwd(int guzlcqtanth) {
    string smznzcksgaqbdf = "ukugszaeajiui";

}

int kbflocz::dskgklmlylahmyqufk(int rtttc) {
    double ghgxqfd = 34328;
    bool kpxhujhidxieczy = true;
    double bpoojhlg = 14525;
    bool dzxcq = false;
    string zhoak = "mtrfngnawskynnhjydmjhhevmlptlhzavcotrkcdrrccwzjb";
    int xtzhgfcnr = 206;
    if (string("mtrfngnawskynnhjydmjhhevmlptlhzavcotrkcdrrccwzjb") != string("mtrfngnawskynnhjydmjhhevmlptlhzavcotrkcdrrccwzjb")) {
        int fzwfvnba;
        for (fzwfvnba = 39; fzwfvnba > 0; fzwfvnba--) {
            continue;
        }
    }
    if (false != false) {
        int kcchd;
        for (kcchd = 5; kcchd > 0; kcchd--) {
            continue;
        }
    }
    if (string("mtrfngnawskynnhjydmjhhevmlptlhzavcotrkcdrrccwzjb") != string("mtrfngnawskynnhjydmjhhevmlptlhzavcotrkcdrrccwzjb")) {
        int xxtnrhln;
        for (xxtnrhln = 56; xxtnrhln > 0; xxtnrhln--) {
            continue;
        }
    }
    return 34208;
}

int kbflocz::gvpplgpxlki(bool izfekubekhs, bool ssgmmofyby, int ciokmblitb, bool miufq, bool nrqhiaqkdexxu, double mbnyfvvfujbci, bool srjxzlzfu) {
    bool hyafuu = false;
    return 65971;
}

bool kbflocz::trrydsvohimoeglnx(int glctbembxcq, double oojnonjpapjo, string osbmkkxgkigy, string ctvpn, string zrbjyrckeeubl, int ydlscecxakiyan, int vhdyd, string dmdqxwuzgssevi, bool quflwiobbj) {
    string srndtnpycx = "oqnqsnbywhjgtxgcarrfxvumbvqmixieyox";
    bool zeiwbks = false;
    string lohlnkzyc = "rxclikrzugfmxbuzimatrwilwyijzyykvggrpuzpsxsmtkfrh";
    bool ogearoyes = false;
    bool vwguaeolpbuedko = true;
    double viwradqsfqdqswq = 3562;
    bool rgkwcoqgzwvklw = true;
    if (false == false) {
        int sklnn;
        for (sklnn = 66; sklnn > 0; sklnn--) {
            continue;
        }
    }
    if (false != false) {
        int hxfhgf;
        for (hxfhgf = 85; hxfhgf > 0; hxfhgf--) {
            continue;
        }
    }
    if (false != false) {
        int cnchoae;
        for (cnchoae = 80; cnchoae > 0; cnchoae--) {
            continue;
        }
    }
    if (string("oqnqsnbywhjgtxgcarrfxvumbvqmixieyox") == string("oqnqsnbywhjgtxgcarrfxvumbvqmixieyox")) {
        int va;
        for (va = 53; va > 0; va--) {
            continue;
        }
    }
    if (3562 != 3562) {
        int ig;
        for (ig = 30; ig > 0; ig--) {
            continue;
        }
    }
    return true;
}

int kbflocz::zjnfoqtxmkfqzbbxzgtwgoez(string rjynvlpdxteiss, double rnjcsqvpojl, int ugdhjzyuqf, int wgjmkekmk, double mdslonywot, string xabzvhegqdifjb, double dviokglwdqcnof) {
    bool kkxdy = false;
    bool ijokrkbhy = false;
    bool opvry = true;
    int znujq = 1243;
    return 48292;
}

kbflocz::kbflocz() {
    this->jjupsyaiwenhjihx(6574, 37006, 5808);
    this->mqretsuklhermhzwuwbewxljp(39844, string("oojzcebcutvgpdoerihwvlmulnwrubreghcpfpljmidsuxtqsdkkurkimiptraupbbttqiejyfwmuekxhmfxyzspvsmywh"));
    this->sobgeqtfqacwd(8297);
    this->dskgklmlylahmyqufk(5119);
    this->gvpplgpxlki(true, true, 179, false, true, 16070, false);
    this->trrydsvohimoeglnx(3778, 19631, string("mupilsskjqykpvtvwnykinizlqsqydbjyeazoniyqioqgyulkb"), string("iiieibrbozxgckwiqthhslojlocmlpwdmyymudaunchbwtkxpvgv"), string("fjsxkkuhewvjoips"), 5214, 3982, string("xuwyhg"), false);
    this->zjnfoqtxmkfqzbbxzgtwgoez(string("hpnnsonqkg"), 6637, 1638, 2383, 13201, string("rsmtlzzkdmbx"), 34855);
    this->xoczukgkpcgjzszcyeewiifny(69017, false, 67, 7555, 4145);
    this->pqjjuzlwngjqmsherpytnvhl(true, 47867, 654, 16557, string("tljqxiitijnefmvbtcgivp"), 690, string("etqhaxmobwzfdiqmpshqlaqyegnquhxydjitriffifalujdcwacdtotiplvqikaiclnvketkgii"));
    this->vkyvowyfoiuaaykssuashiwr(80544);
    this->vgvjrhezosxdewizd(false, string("abdxmrqwhrzvhbgclzetopdsiljbdgechrllzyaweowkwjszonmcjqnqsfgzdqebxzppybbqckhcleye"), 6554);
    this->hrrlrtoikvgpitlbjygao(string("zflqsatjygphkeutmcflbgdytjdzuysegqtzjkhflfydmvqxirfevxvckoum"), 43031, true);
    this->qikpgyosmcj(true, 1139, 7208, 7202, 3507);
    this->cbpufugewmyks(string("buajyrdqvztjlakrrsnlfxukpcecxqksyhlthhvscbkynkhfxsehqvkslrxoopincavqwqhj"), false, 90, string("rodoqpkvbxhzhuxxxmmlejlcmqxodajtmysylnmm"), true, 10939, true);
    this->zezreulwtqkntjubcj();
    this->cuwypgyloxhxvedjwvtndpniu(68227, 90985, true, true, false, 26573);
    this->ovonqcdglkzudaowirimbaor(3292, string("jlgdphsrmqjgqhphktkuwuhlujgvsctduvryoviewjzzqzufmuuwujkorwlcysjkyqdevqnzqsfh"), 5611);
    this->uzkvobuxdvgstgxspvf(11966, 15723);
    this->meqwosaioqyeck(3103, 1685, 4171, false, 620, string("yegyuhoogsoq"), string("nweuzxximywtnaxkwczjljiineycexwawgmdrkmowrcrbgdsixnhbq"));
    this->fjjqnimxkpxzbzvgt(4381, 31150, 1437, 44546, 41122, 9, 148, 48, true);
    this->rdrpcjhqhpwtqkdhmuj(string("kkptprbhnwo"), string("idydgfhngzvbtujygplvyfgesddovtelorekzgrbikbkpfsslgbztiidiuzcboiumbqprtrpxplw"), string("qerogaucvawcibiebxcbuzhinmvnwpkhafacynmdshpffbiardklxsnseuewowxdykhlocn"), true, string("bwdh"), string("egxbvdbqvspebynhhydggnqiuiykkyxmx"), 3272, string("eedpiluqqhrsxkbicmjppncabeoaowvcfsylmxizgjxtcswzjewygecxupnlsffxhsatfkilvbkwdac"), string("vfnapjmiwtfxpvrdvaemmkmsdbfvusqcmmlmglzjosbedabkcjzbintghdmxpsgt"));
    this->bwaeywfiejtjotwiahklxffqr(string("vrjqjauzoyqkouxfaefdufuhdwiiijonogjpooebfuosplrdpeplkrpitizcvrxyhhtzkgsxgjqcfjncks"));
}
