#pragma once

#include "GraphNodeLibrary.h"
#include "GraphEffect.h"

#include <string_view>

namespace Shalltear::GraphEffectBlocks {

	inline constexpr std::string_view PositionShapes[] = {"Sphere", "OrientedBox", "Cone", "Torus", "Circle", "Line", "SignedDistanceField"};
	inline constexpr std::string_view PositionShapeNames[] = {"Sphere", "Box", "Cone / Cylinder", "Torus", "Circle", "Line", "Signed Distance Field"};
	inline constexpr std::string_view PositionModes[] = {"Surface", "Volume", "ThicknessAbsolute", "ThicknessRelative"};
	inline constexpr std::string_view SpawnModes[] = {"Random", "Custom"};
	inline constexpr std::string_view HeightModes[] = {"Base", "Volume"};
	inline constexpr std::string_view Orientations[] = {"None", "Direction", "Axes", "DirectionAndAxes"};

	enum PositionShape : u32 { Sphere = 0,
							   OrientedBox,
							   Cone,
							   Torus,
							   Circle,
							   Line,
							   PositionSDF };

	inline constexpr u32 ALL_POSITION_SHAPES = (1u << std::size(PositionShapes)) - 1u;
	inline constexpr u32 ALL_BUT_LINE = ALL_POSITION_SHAPES & ~(1u << Line);
	inline constexpr u32 ALL_BUT_BOX = ALL_POSITION_SHAPES & ~(1u << OrientedBox);
	inline constexpr u32 DIRECTION_OPTIONS = (1u << 1) | (1u << 3);
	inline constexpr u32 AXES_OPTIONS = (1u << 2) | (1u << 3);
	inline constexpr GraphSettingDefinition PositionShapeSettings[] = {
		{.key = "Shape", .kind = GraphSettingKind::Choice, .options = PositionShapes, .inGraph = true},
		{.key = "Position Mode", .kind = GraphSettingKind::Choice, .options = PositionModes, .inGraph = true, .whenSetting = 0, .whenOptions = ALL_BUT_LINE},
		{.key = "Spawn Mode", .kind = GraphSettingKind::Choice, .options = SpawnModes, .inGraph = true, .whenSetting = 0, .whenOptions = ALL_BUT_BOX},
		{.key = "Height Mode", .kind = GraphSettingKind::Choice, .options = HeightModes, .defaultIndex = 1, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << Cone},
		{.key = "Apply Orientation", .kind = GraphSettingKind::Choice, .options = Orientations, .defaultIndex = 1},
		{.key = "Composition Position", .kind = GraphSettingKind::Choice, .options = GraphEffectCompositions},
		{.key = "Composition Direction", .kind = GraphSettingKind::Choice, .options = GraphEffectCompositions, .whenSetting = 4, .whenOptions = DIRECTION_OPTIONS},
		{.key = "Composition Axes", .kind = GraphSettingKind::Choice, .options = GraphEffectCompositions, .whenSetting = 4, .whenOptions = AXES_OPTIONS},
		{.key = "Kill Outliers", .kind = GraphSettingKind::Toggle, .whenSetting = 0, .whenOptions = 1u << PositionSDF},
		{.key = "Projection Steps", .kind = GraphSettingKind::Number, .defaultValue = {2.0f}, .whenSetting = 0, .whenOptions = 1u << PositionSDF},
	};
	enum PositionShapeSetting : u32 { ShapeSetting = 0,
									  PositionModeSetting,
									  SpawnModeSetting,
									  HeightModeSetting,
									  OrientationSetting,
									  CompositionPositionSetting,
									  CompositionDirectionSetting,
									  CompositionAxesSetting,
									  KillOutliersSetting,
									  ProjectionStepsSetting };

	inline constexpr u32 MAX_PROJECTION_STEPS = 32;

	inline constexpr std::string_view CollisionShapes[] = {"Sphere", "OrientedBox", "Cone", "Plane", "SignedDistanceField"};
	inline constexpr std::string_view CollisionShapeNames[] = {"Sphere", "Box", "Cone / Cylinder", "Plane", "Signed Distance Field"};
	inline constexpr std::string_view CollisionModes[] = {"Solid", "Inverted"};
	inline constexpr std::string_view RadiusModes[] = {"None", "FromSize", "Custom"};
	inline constexpr std::string_view CollisionAttributes[] = {"NoWrite", "WritePunctualContactOnly", "WriteAlways"};
	inline constexpr std::string_view Behaviors[] = {"None", "Collision", "Kill"};
	inline constexpr std::string_view BehaviorNames[] = {"Trigger", "Collision", "Kill"};

	enum CollisionShape : u32 { CollideSphere = 0,
								CollideBox,
								CollideCone,
								CollidePlane,
								CollideSDF };
	enum CollisionBehavior : u32 { BehaviorNone = 0,
								   BehaviorCollision,
								   BehaviorKill };

	inline constexpr GraphSettingDefinition CollisionShapeSettings[] = {
		{.key = "Shape", .kind = GraphSettingKind::Choice, .options = CollisionShapes, .defaultIndex = CollidePlane, .inGraph = true},
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = CollisionModes, .inGraph = true},
		{.key = "Radius Mode", .kind = GraphSettingKind::Choice, .options = RadiusModes, .inGraph = true},
		{.key = "Collision Attributes", .kind = GraphSettingKind::Choice, .options = CollisionAttributes, .inGraph = true},
		{.key = "Rough Surface", .kind = GraphSettingKind::Toggle, .inGraph = true, .whenSetting = 5, .whenOptions = 1u << BehaviorCollision},
		{.key = "Behavior", .kind = GraphSettingKind::Choice, .options = Behaviors, .defaultIndex = BehaviorCollision},
		{.key = "Write Rough Normal", .kind = GraphSettingKind::Toggle, .defaultIndex = 1, .whenSetting = 4, .whenOptions = 1u << 1},
		{.key = "Override Bounce Threshold", .kind = GraphSettingKind::Toggle, .whenSetting = 5, .whenOptions = 1u << BehaviorCollision},
	};
	enum CollisionShapeSetting : u32 { CollisionShapeKey = 0,
									   CollisionModeKey,
									   CollisionRadiusModeKey,
									   CollisionAttributesKey,
									   RoughSurfaceKey,
									   BehaviorKey,
									   WriteRoughNormalKey,
									   OverrideBounceThresholdKey };

	inline constexpr std::string_view SurfaceThicknesses[] = {"Infinite", "Custom"};

	inline constexpr GraphSettingDefinition CollisionDepthSettings[] = {
		{.key = "Surface Thickness", .kind = GraphSettingKind::Choice, .options = SurfaceThicknesses, .inGraph = true},
		{.key = "Radius Mode", .kind = GraphSettingKind::Choice, .options = RadiusModes, .inGraph = true},
		{.key = "Collision Attributes", .kind = GraphSettingKind::Choice, .options = CollisionAttributes, .inGraph = true},
		{.key = "Rough Surface", .kind = GraphSettingKind::Toggle, .inGraph = true, .whenSetting = 4, .whenOptions = 1u << BehaviorCollision},
		{.key = "Behavior", .kind = GraphSettingKind::Choice, .options = Behaviors, .defaultIndex = BehaviorCollision},
		{.key = "Write Rough Normal", .kind = GraphSettingKind::Toggle, .defaultIndex = 1, .whenSetting = 3, .whenOptions = 1u << 1},
		{.key = "Override Bounce Threshold", .kind = GraphSettingKind::Toggle, .whenSetting = 4, .whenOptions = 1u << BehaviorCollision},
	};
	enum CollisionDepthSetting : u32 { SurfaceThicknessKey = 0,
									   DepthRadiusModeKey,
									   DepthAttributesKey,
									   DepthRoughSurfaceKey,
									   DepthBehaviorKey,
									   DepthWriteRoughNormalKey,
									   DepthOverrideBounceThresholdKey };

	inline constexpr std::string_view DepthPositionModes[] = {"Random", "Sequential", "Custom"};
	inline constexpr std::string_view DepthCullModes[] = {"None", "Far Plane", "Range"};
	enum DepthPositionMode : u32 { DepthRandom = 0,
								   DepthSequential,
								   DepthCustom };
	enum DepthCullMode : u32 { DepthCullNone = 0,
							   DepthCullFarPlane,
							   DepthCullRange };

	inline constexpr GraphSettingDefinition PositionDepthSettings[] = {
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = DepthPositionModes, .inGraph = true},
		{.key = "Cull Mode", .kind = GraphSettingKind::Choice, .options = DepthCullModes, .inGraph = true},
		{.key = "Composition Position", .kind = GraphSettingKind::Choice, .options = GraphEffectCompositions},
	};
	enum PositionDepthSetting : u32 { DepthPositionModeKey = 0,
									  DepthCullModeKey,
									  DepthCompositionKey };

	inline constexpr std::string_view ForceModes[] = {"Absolute", "Relative"};

	inline constexpr GraphSettingDefinition ForceSettings[] = {{.key = "Mode", .kind = GraphSettingKind::Choice, .options = ForceModes, .inGraph = true}};
	inline constexpr GraphPortDefinition ForceInputs[] = {
		{.name = "Force", .type = GraphPortType::Float3, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = 1u << 0, .space = GraphPortSpace::Vector},
		{.name = "Velocity", .type = GraphPortType::Float3, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = 1u << 1, .space = GraphPortSpace::Vector},
		{.name = "Drag", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = 1u << 1},
	};
	inline constexpr GraphPortDefinition GravityInputs[] = {
		{.name = "Force", .type = GraphPortType::Float3, .defaultValue = {0.0f, -9.81f, 0.0f, 0.0f}, .space = GraphPortSpace::Vector}};

	inline constexpr GraphSettingDefinition LinearDragSettings[] = {{.key = "Use Particle Size", .kind = GraphSettingKind::Toggle, .inGraph = true}};
	inline constexpr GraphPortDefinition LinearDragInputs[] = {{.name = "Drag Coefficient", .type = GraphPortType::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}}};

	inline constexpr GraphSettingDefinition TurbulenceSettings[] = {
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = ForceModes, .defaultIndex = 1, .inGraph = true},
		{.key = "Noise Type", .kind = GraphSettingKind::Choice, .options = GraphEffectNoiseTypes, .inGraph = true},
	};
	inline constexpr GraphPortDefinition TurbulenceInputs[] = {
		{.name = "Position", .type = GraphPortType::Float3, .label = "Field Position", .space = GraphPortSpace::Matrix},
		{.name = "Angles", .type = GraphPortType::Float3, .label = "Field Angles"},
		{.name = "Scale", .type = GraphPortType::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}, .label = "Field Scale"},
		{.name = "Intensity", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Drag", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = 1u << 1},
		{.name = "Frequency", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Octaves", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Roughness", .type = GraphPortType::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}},
		{.name = "Lacunarity", .type = GraphPortType::Float, .defaultValue = {2.0f, 0.0f, 0.0f, 0.0f}},
	};

	inline constexpr GraphPortDefinition ConformToSphereInputs[] = {
		{.name = "Center", .type = GraphPortType::Float3, .space = GraphPortSpace::Position},
		{.name = "Radius", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Attraction Speed", .type = GraphPortType::Float, .defaultValue = {5.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Attraction Force", .type = GraphPortType::Float, .defaultValue = {20.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Stick Distance", .type = GraphPortType::Float, .defaultValue = {0.1f, 0.0f, 0.0f, 0.0f}},
		{.name = "Stick Force", .type = GraphPortType::Float, .defaultValue = {50.0f, 0.0f, 0.0f, 0.0f}},
	};

	inline constexpr GraphPortDefinition VelocityOutput[] = {{.name = "velocity", .type = GraphPortType::Float3}};

	inline constexpr GraphPortDefinition FieldTransformInputs[] = {
		{.name = "Center", .type = GraphPortType::Float3, .label = "Field Center", .space = GraphPortSpace::Matrix},
		{.name = "Angles", .type = GraphPortType::Float3, .label = "Field Angles"},
		{.name = "Size", .type = GraphPortType::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}, .label = "Field Size"},
	};

	inline constexpr std::string_view DataEncodings[] = {"UnsignedNormalized", "Signed"};

	inline constexpr GraphSettingDefinition VectorFieldSettings[] = {
		{.key = "Data Encoding", .kind = GraphSettingKind::Choice, .options = DataEncodings, .inGraph = true},
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = ForceModes, .defaultIndex = 1, .inGraph = true},
		{.key = "Closed Field", .kind = GraphSettingKind::Toggle},
		{.key = "Conserve Magnitude", .kind = GraphSettingKind::Toggle},
	};
	enum VectorFieldSetting : u32 { DataEncodingKey = 0,
									VectorFieldModeKey,
									ClosedFieldKey,
									ConserveMagnitudeKey };
	inline constexpr GraphPortDefinition VectorFieldInputs[] = {
		{.name = "Vector Field", .type = GraphPortType::Texture3D},
		FieldTransformInputs[0],
		FieldTransformInputs[1],
		FieldTransformInputs[2],
		{.name = "Intensity", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Drag", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 1, .whenOptions = 1u << 1},
	};

	inline constexpr GraphPortDefinition ConformToSDFInputs[] = {
		{.name = "Distance Field", .type = GraphPortType::Texture3D},
		FieldTransformInputs[0],
		FieldTransformInputs[1],
		FieldTransformInputs[2],
		ConformToSphereInputs[2],
		ConformToSphereInputs[3],
		ConformToSphereInputs[4],
		ConformToSphereInputs[5],
	};

	inline constexpr GraphPortDefinition SampleSDFInputs[] = {
		{.name = "Texture", .type = GraphPortType::Texture3D},
		{.name = "Center", .type = GraphPortType::Float3, .label = "Oriented Box Center", .space = GraphPortSpace::Matrix},
		{.name = "Angles", .type = GraphPortType::Float3, .label = "Oriented Box Angles"},
		{.name = "Size", .type = GraphPortType::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}, .label = "Oriented Box Size"},
		{.name = "Position", .type = GraphPortType::Float3, .space = GraphPortSpace::Position},
		{.name = "Mip Level", .type = GraphPortType::Float},
	};
	inline constexpr GraphPortDefinition SampleSDFOutputs[] = {{.name = "distance", .type = GraphPortType::Float}, {.name = "direction", .type = GraphPortType::Float3}};

	inline constexpr std::string_view OrientModes[] = {"FaceCameraPlane", "FaceCameraPosition", "LookAtPosition", "LookAtLine", "Advanced",
													   "FixedAxis", "AlongVelocity", "CustomZ", "CustomY"};
	inline constexpr std::string_view OrientModeNames[] = {"Face Camera Plane", "Face Camera Position", "Look At Position", "Look At Line", "Advanced",
														   "Fixed Axis", "Along Velocity", "Custom Z", "Custom Y"};
	inline constexpr std::string_view OrientAxesOptions[] = {"XY", "YZ", "ZX", "YX", "ZY", "XZ"};

	enum OrientMode : u32 { FaceCameraPlane = 0,
							FaceCameraPosition,
							LookAtPosition,
							LookAtLine,
							OrientAdvanced,
							FixedAxis,
							AlongVelocity,
							CustomZ,
							CustomY };
	enum OrientAxes : u32 { AxesXY = 0,
							AxesYZ,
							AxesZX,
							AxesYX,
							AxesZY,
							AxesXZ };

	inline constexpr GraphSettingDefinition OrientSettings[] = {
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = OrientModes, .inGraph = true},
		{.key = "Axes", .kind = GraphSettingKind::Choice, .options = OrientAxesOptions, .defaultIndex = AxesZY, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << OrientAdvanced},
	};
	inline constexpr GraphPortDefinition AxesOutputs[] = {
		{.name = "axisX", .type = GraphPortType::Float3}, {.name = "axisY", .type = GraphPortType::Float3}, {.name = "axisZ", .type = GraphPortType::Float3}};

	inline constexpr std::string_view FlipbookModes[] = {"FrameRate", "Cycles"};
	inline constexpr std::string_view FrameRateModes[] = {"Constant", "Random", "OverLifetime", "BySpeed", "Custom"};
	inline constexpr std::string_view CyclesModes[] = {"Constant", "Random", "RandomFullCycle"};
	inline constexpr std::string_view AnimationRanges[] = {"EntireFlipbook", "FlipbookRow", "FlipbookColumn", "StartEndFrames"};

	enum FlipbookMode : u32 { ByFrameRate = 0,
							  ByCycles };
	enum FrameRateMode : u32 { RateConstant = 0,
							   RateRandom,
							   RateOverLifetime,
							   RateBySpeed,
							   RateCustom };
	enum CyclesMode : u32 { CyclesConstant = 0,
							CyclesRandom,
							CyclesRandomFullCycle };
	enum AnimationRange : u32 { EntireFlipbook = 0,
								FlipbookRow,
								FlipbookColumn,
								StartEndFrames };

	inline constexpr GraphSettingDefinition FlipbookPlayerSettings[] = {
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = FlipbookModes, .inGraph = true},
		{.key = "Frame Rate Mode", .kind = GraphSettingKind::Choice, .options = FrameRateModes, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << ByFrameRate},
		{.key = "Cycles Mode", .kind = GraphSettingKind::Choice, .options = CyclesModes, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << ByCycles},
		{.key = "Use Custom Range", .kind = GraphSettingKind::Toggle, .whenSetting = 0, .whenOptions = 1u << ByFrameRate},
		{.key = "Animation Range", .kind = GraphSettingKind::Choice, .options = AnimationRanges, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << ByCycles, .orSetting = 3, .orOptions = 1u << 1},
		{.key = "Reverse", .kind = GraphSettingKind::Toggle},
		{.key = "Clamp Blending", .kind = GraphSettingKind::Toggle, .whenSetting = 0, .whenOptions = 1u << ByCycles},
		{.key = "Custom Curve", .kind = GraphSettingKind::Toggle, .whenSetting = 0, .whenOptions = 1u << ByCycles},
	};
	enum FlipbookPlayerSetting : u32 { FlipbookModeKey = 0,
									   FrameRateModeKey,
									   CyclesModeKey,
									   UseCustomRangeKey,
									   AnimationRangeKey,
									   ReverseKey,
									   ClampBlendingKey,
									   CustomCurveKey };
	inline constexpr GraphPortDefinition FlipbookPlayerInputs[] = {{.name = "Frame Rate", .type = GraphPortType::Float, .defaultValue = {24.0f, 0.0f, 0.0f, 0.0f}}};
	inline constexpr GraphPortDefinition TexIndexOutput[] = {{.name = "texIndex", .type = GraphPortType::Float}};

	inline constexpr std::string_view MeshPlacements[] = {"Vertex", "Edge", "Surface"};
	inline constexpr std::string_view SurfaceCoordinatesOptions[] = {"Barycentric", "Uniform"};
	inline constexpr std::string_view AddressingModes[] = {"Wrap", "Clamp", "Mirror"};

	enum MeshPlacement : u32 { PlaceVertex = 0,
							   PlaceEdge,
							   PlaceSurface };
	enum SurfaceCoordinates : u32 { SurfaceBarycentric = 0,
									SurfaceUniform };
	enum AddressingMode : u32 { AddressWrap = 0,
								AddressClamp,
								AddressMirror };

	inline constexpr std::string_view MeshOutputs[] = {"Position", "Normal", "Tangent", "Bitangent", "Bitangent Sign", "Color", "Tex Coord 0"};
	enum MeshOutput : u32 { OutPosition = 0,
							OutNormal,
							OutTangent,
							OutBitangent,
							OutBitangentSign,
							OutColor,
							OutTexCoord0 };
	inline constexpr u32 MESH_OUTPUT_COUNT = static_cast<u32>(std::size(MeshOutputs));

	inline constexpr GraphSettingDefinition SampleMeshSettings[] = {
		{.key = "Output Position", .kind = GraphSettingKind::Toggle, .defaultIndex = 1},
		{.key = "Output Normal", .kind = GraphSettingKind::Toggle},
		{.key = "Output Tangent", .kind = GraphSettingKind::Toggle},
		{.key = "Output Bitangent", .kind = GraphSettingKind::Toggle},
		{.key = "Output Bitangent Sign", .kind = GraphSettingKind::Toggle},
		{.key = "Output Color", .kind = GraphSettingKind::Toggle, .defaultIndex = 1},
		{.key = "Output Tex Coord 0", .kind = GraphSettingKind::Toggle, .defaultIndex = 1},
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = AddressingModes, .defaultIndex = AddressClamp, .inGraph = true},
		{.key = "Placement Mode", .kind = GraphSettingKind::Choice, .options = MeshPlacements, .inGraph = true},
		{.key = "Surface Coordinates", .kind = GraphSettingKind::Choice, .options = SurfaceCoordinatesOptions, .defaultIndex = SurfaceUniform, .inGraph = true, .whenSetting = 8, .whenOptions = 1u << PlaceSurface},
	};
	enum SampleMeshSetting : u32 { SampleMeshModeKey = MESH_OUTPUT_COUNT,
								   SampleMeshPlacementKey,
								   SampleMeshSurfaceKey };

	inline constexpr std::string_view AxesCompositions[] = {"Set", "Blend"};
	inline constexpr GraphSettingDefinition PositionMeshSettings[] = {
		{.key = "Spawn Mode", .kind = GraphSettingKind::Choice, .options = SpawnModes, .inGraph = true},
		{.key = "Placement Mode", .kind = GraphSettingKind::Choice, .options = MeshPlacements, .inGraph = true},
		{.key = "Surface Coordinates", .kind = GraphSettingKind::Choice, .options = SurfaceCoordinatesOptions, .defaultIndex = SurfaceUniform, .inGraph = true, .whenSetting = 1, .whenOptions = 1u << PlaceSurface},
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = AddressingModes, .defaultIndex = AddressClamp, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << 1},
		{.key = "Apply Orientation", .kind = GraphSettingKind::Choice, .options = Orientations, .defaultIndex = 1},
		{.key = "Composition Position", .kind = GraphSettingKind::Choice, .options = GraphEffectCompositions},
		{.key = "Composition Direction", .kind = GraphSettingKind::Choice, .options = GraphEffectCompositions, .whenSetting = 4, .whenOptions = DIRECTION_OPTIONS},
		{.key = "Composition Axes", .kind = GraphSettingKind::Choice, .options = AxesCompositions, .whenSetting = 4, .whenOptions = AXES_OPTIONS},
	};
	enum PositionMeshSetting : u32 { MeshSpawnModeKey = 0,
									 MeshPlacementKey,
									 MeshSurfaceKey,
									 MeshAddressingKey,
									 MeshOrientationKey,
									 MeshCompositionPositionKey,
									 MeshCompositionDirectionKey,
									 MeshCompositionAxesKey };

	inline constexpr GraphPortDefinition MeshTransformInputs[] = {
		{.name = "Transform Position", .type = GraphPortType::Float3, .space = GraphPortSpace::Matrix},
		{.name = "Transform Angles", .type = GraphPortType::Float3},
		{.name = "Transform Scale", .type = GraphPortType::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}},
	};
	inline constexpr GraphPortDefinition MeshInput[] = {{.name = "Mesh", .type = GraphPortType::Mesh}};
	inline constexpr GraphPortDefinition SampleMeshIndexInputs[] = {MeshInput[0], {.name = "Index", .type = GraphPortType::Float}};
	inline constexpr GraphPortDefinition SampleMeshIndexOutputs[] = {{.name = "Index", .type = GraphPortType::Float}};
	inline constexpr GraphPortDefinition MeshCountOutputs[] = {{.name = "Count", .type = GraphPortType::Float}};

	inline constexpr std::string_view SequentialShapes[] = {"Line", "Circle", "ThreeDimensional"};
	inline constexpr std::string_view SequentialIndexModes[] = {"ParticleID", "Custom"};
	enum SequentialShape : u32 { SequentialLine = 0,
								 SequentialCircle,
								 SequentialThreeDimensional };
	inline constexpr GraphSettingDefinition PositionSequentialSettings[] = {
		{.key = "Composition Position", .kind = GraphSettingKind::Choice, .options = GraphEffectCompositions, .defaultIndex = 1, .whenSetting = 5, .whenOptions = 1u << 1},
		{.key = "Composition Direction", .kind = GraphSettingKind::Choice, .options = GraphEffectCompositions, .whenSetting = 5, .whenOptions = 1u << 1},
		{.key = "Composition Target Position", .kind = GraphSettingKind::Choice, .options = GraphEffectCompositions, .defaultIndex = 1, .whenSetting = 6, .whenOptions = 1u << 1},
		{.key = "Shape", .kind = GraphSettingKind::Choice, .options = SequentialShapes},
		{.key = "Index", .kind = GraphSettingKind::Choice, .options = SequentialIndexModes, .inGraph = true},
		{.key = "Write Position", .kind = GraphSettingKind::Toggle, .defaultIndex = 1, .inGraph = true},
		{.key = "Write Target Position", .kind = GraphSettingKind::Toggle, .inGraph = true},
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = AddressingModes, .defaultIndex = AddressClamp, .inGraph = true},
	};
	enum PositionSequentialSetting : u32 { SequentialCompositionPositionKey = 0,
										   SequentialCompositionDirectionKey,
										   SequentialCompositionTargetKey,
										   SequentialShapeKey,
										   SequentialIndexKey,
										   SequentialWritePositionKey,
										   SequentialWriteTargetKey,
										   SequentialModeKey };

	inline constexpr std::string_view CameraFadeModes[] = {"Color", "Alpha", "ColorAndAlpha"};
	enum CameraFadeMode : u32 { FadeColor = 0,
								FadeAlpha,
								FadeColorAndAlpha };
	inline constexpr GraphSettingDefinition CameraFadeSettings[] = {
		{.key = "Fade Mode", .kind = GraphSettingKind::Choice, .options = CameraFadeModes, .defaultIndex = FadeAlpha, .inGraph = true},
		{.key = "Cull When Faded", .kind = GraphSettingKind::Toggle, .defaultIndex = 1},
		{.key = "Affect Shadows", .kind = GraphSettingKind::Toggle},
	};
	inline constexpr GraphPortDefinition CameraFadeInputs[] = {
		{.name = "Faded Distance", .type = GraphPortType::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}},
		{.name = "Visible Distance", .type = GraphPortType::Float, .defaultValue = {2.0f, 0.0f, 0.0f, 0.0f}},
	};
	inline constexpr GraphPortDefinition CameraFadeOutputs[] = {
		{.name = "color", .type = GraphPortType::Float3}, {.name = "alpha", .type = GraphPortType::Float}, {.name = "alive", .type = GraphPortType::Boolean}};

	enum TriggerMode : u32 { TriggerAlways = 0,
							 TriggerOverTime,
							 TriggerOverDistance,
							 TriggerOnDie,
							 TriggerOnCollide };
	inline constexpr std::string_view TriggerModeNames[] = {"Always", "Over Time", "Over Distance", "On Die", "On Collide"};
	inline constexpr u32 TRIGGER_COUNT_MODES = (1u << TriggerAlways) | (1u << TriggerOnDie) | (1u << TriggerOnCollide);
	inline constexpr u32 TRIGGER_RATE_MODES = (1u << TriggerOverTime) | (1u << TriggerOverDistance);
	inline constexpr GraphSettingDefinition TriggerEventSettings[] = {
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = GraphEffectTriggerModes, .inGraph = true},
		{.key = "Clamp To One", .kind = GraphSettingKind::Toggle, .defaultIndex = 1, .whenSetting = 0, .whenOptions = TRIGGER_RATE_MODES},
	};
	inline constexpr GraphPortDefinition TriggerEventInputs[] = {
		{.name = "Count", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = TRIGGER_COUNT_MODES},
		{.name = "Rate", .type = GraphPortType::Float, .defaultValue = {10.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = TRIGGER_RATE_MODES},
	};
	inline constexpr GraphPortDefinition TriggerEventOutputs[] = {
		{.name = GraphEffectEvt, .type = GraphPortType::GPUEvent},
		{.name = GraphEffectRateCount, .type = GraphPortType::Float},
	};

	struct ElementSphere {
		GraphValueId center = GRAPH_NO_VALUE;
		GraphValueId radius = GRAPH_NO_VALUE;
	};
	ElementSphere ElementBounds(GraphProgramBuilder& b, GraphValueId position, GraphValueId size, GraphValueId scale, GraphValueId angle,
								GraphValueId pivot, GraphValueId axisX, GraphValueId axisY, GraphValueId axisZ);

	void TriggerEventBody(GraphBodyContext& c);
	void CameraFadeBody(GraphBodyContext& c);
	void PositionShapeBody(GraphBodyContext& c);
	void PositionMeshBody(GraphBodyContext& c);
	void PositionSequentialBody(GraphBodyContext& c);
	void SampleMeshBody(GraphBodyContext& c);
	void SampleMeshIndexBody(GraphBodyContext& c);
	void MeshVertexCountBody(GraphBodyContext& c);
	void MeshIndexCountBody(GraphBodyContext& c);
	void MeshTriangleCountBody(GraphBodyContext& c);
	void CollisionShapeBody(GraphBodyContext& c);
	void CollisionDepthBody(GraphBodyContext& c);
	void PositionDepthBody(GraphBodyContext& c);
	void ForceBody(GraphBodyContext& c);
	void GravityBody(GraphBodyContext& c);
	void LinearDragBody(GraphBodyContext& c);
	void TurbulenceBody(GraphBodyContext& c);
	void ConformToSphereBody(GraphBodyContext& c);
	void OrientBody(GraphBodyContext& c);
	void FlipbookPlayerBody(GraphBodyContext& c);
	void VectorFieldForceBody(GraphBodyContext& c);
	void ConformToSDFBody(GraphBodyContext& c);
	void SampleSDFBody(GraphBodyContext& c);

} // namespace Shalltear::GraphEffectBlocks
