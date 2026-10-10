#pragma once
#include <string>
#include <vector>

namespace pple::viewer {

// The six tool machines of the Comfy Cakes room.  A <Tool> element of COMFYCAKESGAME.XML names
// a document (UI\COMFYCAKES\TOOLS\BATTERTOOL.XML and its five siblings) that
// ComfyCakesUILoader::LoadTool (0x100061AD4) reads: Tool::Init (0x100074D0C) builds the
// button-area Base and the machine Base, takes <Station>, loads <HighlightSprite> and attaches
// MachineUnusableShakeAnim.xml to the machine, and the subclass then adds its own parts:
//
//   CakePanTool      <FallingSprite> CakePanSquishSprite,     <ActionSprite> CakePanMachineSprite
//   BatterTool       <FallingSprite> FallingBatterSprite,     <ActionSprite> BatterMachineSprite,
//                    plus Round/Rectangle/HeartFillingSprite, which ride the cake itself
//   FillingTool      <SplatSprite>   FillingSplatSprite,      <BaseSprite>  FillingMachineTubes,
//                    <ActionSprite>  FillingMachineSprite
//   IcingTool        <SplatSprite>   IcingSplatSprite,        <StreamSprite> IcingStreamSprite,
//                    <ActionSprite>  IcingMachineActionSprite
//   DecorationTool   <FallingSprite> FallingDecorationsSprite,<ActionSprite> DecorationMachineSprite
//   FinalTouchTool   <ActionSprite>  FinalTouchMachineAction, <BaseSprite>   FinalTouchMachineBase
//
// Each part carries one animation per flavour of its tool (the button's <Type>), or a single one
// when the part has only one.  A click on a button plays the flavour's animation on every part
// that has one and applies the layer when the longest one finishes; a click the cake cannot
// answer shakes the machine Base instead, which is what Tool::UseTool's refusal branch
// (0x1000753B4) resumes.

// What one block of a ResourceAnimation drives.  <Affects> names the property, <Type Name>
// says whether it is an absolute run (Start -> End) or a relative one (Count), and
// <WaitForComplete/> separates the blocks.  Every shipped Comfy Cakes set uses it, so the runs
// of one part's animation concatenate into a single timeline.
struct CakeAnimRun {
    enum class Affects { Frame, X, Y, Alpha, Position };
    Affects affects = Affects::Frame;
    float from = 0.0f;
    float to = 0.0f;
    float seconds = 0.0f;
    // A relative run moves the property by its value instead of setting it, so the blocks of a
    // shake (+6, -12, +6) accumulate and the machine ends where it started.
    bool relative = false;
};

// One animation of one machine part: its timeline, and how long it takes end to end.
struct CakeAnimSet {
    std::vector<CakeAnimRun> runs;
    float total = 0.0f;
};

// Which number picks an animation out of a part's list.
enum class CakePartIndex { Single, ByType, ByShape };

// One part of a machine.
struct CakeToolPart {
    std::string sprite;                  // UI\ComfyCakes\Machines\....xml
    float x = 0.0f, y = 0.0f;            // inside the tool's own container
    CakePartIndex index_from = CakePartIndex::Single;
    int type_base = 0;                   // the <Type> the part's first animation serves
    std::vector<CakeAnimSet> anims;
    // The batter tool's fill sprites are re-parented onto the cake (PlayAddToolAnimation
    // 0x100076050, NodeBase::ImportNode) so they draw inside the cake's own coordinate space.
    bool rides_the_cake = false;
    // <RegistrationOffsetX/Y>: the three batter fill sprites carry it on every <Animation>, and
    // it is what centres the growing ingredient on the cake rather than beside it.
    float reg_off_x = 0.0f, reg_off_y = 0.0f;
};

// One tool machine.
struct CakeTool {
    std::string name;                    // the COMFYCAKESGAME.XML <Tool Name=...> value
    int station = -1;                    // its own <Station>
    std::vector<CakeToolPart> parts;
    CakeAnimSet shake;                   // MachineUnusableShakeAnim.xml, on the machine Base
};

// One part of the animation currently playing, with the flavour index it plays and the
// direction the engine runs it in (ToolIcing::PlayRotateAnimation reverses half the pairs).
struct CakeToolPlaying {
    const CakeToolPart* part = nullptr;
    std::size_t index = 0;
    bool reverse = false;
};

}  // namespace pple::viewer
