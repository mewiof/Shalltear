#pragma once

#include "GraphNodeLibrary.h"
#include "GraphEffect.h"

#include <string_view>

namespace Shalltear::GraphEffectBlocks {

	inline constexpr std::string_view POSITION_SHAPES[] = {"Sphere", "OrientedBox", "Cone", "Torus", "Circle", "Line", "SignedDistanceField"};
	inline constexpr std::string_view POSITION_SHAPE_NAMES[] = {"Sphere", "Box", "Cone / Cylinder", "Torus", "Circle", "Line", "Signed Distance Field"};
	inline constexpr std::string_view POSITION_MODES[] = {"Surface", "Volume", "ThicknessAbsolute", "ThicknessRelative"};
	inline constexpr std::string_view SPAWN_MODES[] = {"Random", "Custom"};
	inline constexpr std::string_view HEIGHT_MODES[] = {"Base", "Volume"};
	inline constexpr std::string_view ORIENTATIONS[] = {"None", "Direction", "Axes", "DirectionAndAxes"};

	enum PositionShape : u32 { Sphere = 0,
							   OrientedBox,
							   Cone,
							   Torus,
							   Circle,
							   Line,
							   PositionSDF };

	inline constexpr u32 ALL_POSITION_SHAPES = (1u << std::size(POSITION_SHAPES)) - 1u;
	inline constexpr u32 ALL_BUT_LINE = ALL_POSITION_SHAPES & ~(1u << Line);
	inline constexpr u32 ALL_BUT_BOX = ALL_POSITION_SHAPES & ~(1u << OrientedBox);
	inline constexpr u32 DIRECTION_OPTIONS = (1u << 1) | (1u << 3);
	inline constexpr u32 AXES_OPTIONS = (1u << 2) | (1u << 3);
	inline constexpr GraphSettingDefinition POSITION_SHAPE_SETTINGS[] = {
		{.key = "Shape", .kind = GraphSettingKind::Choice, .options = POSITION_SHAPES, .inGraph = true},
		{.key = "Position Mode", .kind = GraphSettingKind::Choice, .options = POSITION_MODES, .inGraph = true, .whenSetting = 0, .whenOptions = ALL_BUT_LINE},
		{.key = "Spawn Mode", .kind = GraphSettingKind::Choice, .options = SPAWN_MODES, .inGraph = true, .whenSetting = 0, .whenOptions = ALL_BUT_BOX},
		{.key = "Height Mode", .kind = GraphSettingKind::Choice, .options = HEIGHT_MODES, .defaultIndex = 1, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << Cone},
		{.key = "Apply Orientation", .kind = GraphSettingKind::Choice, .options = ORIENTATIONS, .defaultIndex = 1},
		{.key = "Composition Position", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_COMPOSITIONS},
		{.key = "Composition Direction", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_COMPOSITIONS, .whenSetting = 4, .whenOptions = DIRECTION_OPTIONS},
		{.key = "Composition Axes", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_COMPOSITIONS, .whenSetting = 4, .whenOptions = AXES_OPTIONS},
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

	inline constexpr std::string_view COLLISION_SHAPES[] = {"Sphere", "OrientedBox", "Cone", "Plane", "SignedDistanceField"};
	inline constexpr std::string_view COLLISION_SHAPE_NAMES[] = {"Sphere", "Box", "Cone / Cylinder", "Plane", "Signed Distance Field"};
	inline constexpr std::string_view COLLISION_MODES[] = {"Solid", "Inverted"};
	inline constexpr std::string_view RADIUS_MODES[] = {"None", "FromSize", "Custom"};
	inline constexpr std::string_view COLLISION_ATTRIBUTES[] = {"NoWrite", "WritePunctualContactOnly", "WriteAlways"};
	inline constexpr std::string_view BEHAVIORS[] = {"None", "Collision", "Kill"};
	inline constexpr std::string_view BEHAVIOR_NAMES[] = {"Trigger", "Collision", "Kill"};

	enum CollisionShape : u32 { CollideSphere = 0,
								CollideBox,
								CollideCone,
								CollidePlane,
								CollideSDF };
	enum CollisionBehavior : u32 { BehaviorNone = 0,
								   BehaviorCollision,
								   BehaviorKill };

	inline constexpr GraphSettingDefinition COLLISION_SHAPE_SETTINGS[] = {
		{.key = "Shape", .kind = GraphSettingKind::Choice, .options = COLLISION_SHAPES, .defaultIndex = CollidePlane, .inGraph = true},
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = COLLISION_MODES, .inGraph = true},
		{.key = "Radius Mode", .kind = GraphSettingKind::Choice, .options = RADIUS_MODES, .inGraph = true},
		{.key = "Collision Attributes", .kind = GraphSettingKind::Choice, .options = COLLISION_ATTRIBUTES, .inGraph = true},
		{.key = "Rough Surface", .kind = GraphSettingKind::Toggle, .inGraph = true, .whenSetting = 5, .whenOptions = 1u << BehaviorCollision},
		{.key = "Behavior", .kind = GraphSettingKind::Choice, .options = BEHAVIORS, .defaultIndex = BehaviorCollision},
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

	inline constexpr std::string_view SURFACE_THICKNESSES[] = {"Infinite", "Custom"};

	inline constexpr GraphSettingDefinition COLLISION_DEPTH_SETTINGS[] = {
		{.key = "Surface Thickness", .kind = GraphSettingKind::Choice, .options = SURFACE_THICKNESSES, .inGraph = true},
		{.key = "Radius Mode", .kind = GraphSettingKind::Choice, .options = RADIUS_MODES, .inGraph = true},
		{.key = "Collision Attributes", .kind = GraphSettingKind::Choice, .options = COLLISION_ATTRIBUTES, .inGraph = true},
		{.key = "Rough Surface", .kind = GraphSettingKind::Toggle, .inGraph = true, .whenSetting = 4, .whenOptions = 1u << BehaviorCollision},
		{.key = "Behavior", .kind = GraphSettingKind::Choice, .options = BEHAVIORS, .defaultIndex = BehaviorCollision},
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

	inline constexpr std::string_view DEPTH_POSITION_MODES[] = {"Random", "Sequential", "Custom"};
	inline constexpr std::string_view DEPTH_CULL_MODES[] = {"None", "Far Plane", "Range"};
	enum DepthPositionMode : u32 { DepthRandom = 0,
								   DepthSequential,
								   DepthCustom };
	enum DepthCullMode : u32 { DepthCullNone = 0,
							   DepthCullFarPlane,
							   DepthCullRange };

	inline constexpr GraphSettingDefinition POSITION_DEPTH_SETTINGS[] = {
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = DEPTH_POSITION_MODES, .inGraph = true},
		{.key = "Cull Mode", .kind = GraphSettingKind::Choice, .options = DEPTH_CULL_MODES, .inGraph = true},
		{.key = "Composition Position", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_COMPOSITIONS},
	};
	enum PositionDepthSetting : u32 { DepthPositionModeKey = 0,
									  DepthCullModeKey,
									  DepthCompositionKey };

	inline constexpr std::string_view FORCE_MODES[] = {"Absolute", "Relative"};

	inline constexpr GraphSettingDefinition FORCE_SETTINGS[] = {{.key = "Mode", .kind = GraphSettingKind::Choice, .options = FORCE_MODES, .inGraph = true}};
	inline constexpr GraphPortDefinition FORCE_INPUTS[] = {
		{.name = "Force", .type = GraphPortType::Float3, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = 1u << 0, .space = GraphPortSpace::Vector},
		{.name = "Velocity", .type = GraphPortType::Float3, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = 1u << 1, .space = GraphPortSpace::Vector},
		{.name = "Drag", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = 1u << 1},
	};
	inline constexpr GraphPortDefinition GRAVITY_INPUTS[] = {
		{.name = "Force", .type = GraphPortType::Float3, .defaultValue = {0.0f, -9.81f, 0.0f, 0.0f}, .space = GraphPortSpace::Vector}};

	inline constexpr GraphSettingDefinition LINEAR_DRAG_SETTINGS[] = {{.key = "Use Particle Size", .kind = GraphSettingKind::Toggle, .inGraph = true}};
	inline constexpr GraphPortDefinition LINEAR_DRAG_INPUTS[] = {{.name = "Drag Coefficient", .type = GraphPortType::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}}};

	inline constexpr GraphSettingDefinition TURBULENCE_SETTINGS[] = {
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = FORCE_MODES, .defaultIndex = 1, .inGraph = true},
		{.key = "Noise Type", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_NOISE_TYPES, .inGraph = true},
	};
	inline constexpr GraphPortDefinition TURBULENCE_INPUTS[] = {
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

	inline constexpr GraphPortDefinition CONFORM_TO_SPHERE_INPUTS[] = {
		{.name = "Center", .type = GraphPortType::Float3, .space = GraphPortSpace::Position},
		{.name = "Radius", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Attraction Speed", .type = GraphPortType::Float, .defaultValue = {5.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Attraction Force", .type = GraphPortType::Float, .defaultValue = {20.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Stick Distance", .type = GraphPortType::Float, .defaultValue = {0.1f, 0.0f, 0.0f, 0.0f}},
		{.name = "Stick Force", .type = GraphPortType::Float, .defaultValue = {50.0f, 0.0f, 0.0f, 0.0f}},
	};

	inline constexpr GraphPortDefinition VELOCITY_OUTPUT[] = {{.name = "velocity", .type = GraphPortType::Float3}};

	inline constexpr GraphPortDefinition FIELD_TRANSFORM_INPUTS[] = {
		{.name = "Center", .type = GraphPortType::Float3, .label = "Field Center", .space = GraphPortSpace::Matrix},
		{.name = "Angles", .type = GraphPortType::Float3, .label = "Field Angles"},
		{.name = "Size", .type = GraphPortType::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}, .label = "Field Size"},
	};

	inline constexpr std::string_view DATA_ENCODINGS[] = {"UnsignedNormalized", "Signed"};

	inline constexpr GraphSettingDefinition VECTOR_FIELD_SETTINGS[] = {
		{.key = "Data Encoding", .kind = GraphSettingKind::Choice, .options = DATA_ENCODINGS, .inGraph = true},
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = FORCE_MODES, .defaultIndex = 1, .inGraph = true},
		{.key = "Closed Field", .kind = GraphSettingKind::Toggle},
		{.key = "Conserve Magnitude", .kind = GraphSettingKind::Toggle},
	};
	enum VectorFieldSetting : u32 { DataEncodingKey = 0,
									VectorFieldModeKey,
									ClosedFieldKey,
									ConserveMagnitudeKey };
	inline constexpr GraphPortDefinition VECTOR_FIELD_INPUTS[] = {
		{.name = "Vector Field", .type = GraphPortType::Texture3D},
		FIELD_TRANSFORM_INPUTS[0],
		FIELD_TRANSFORM_INPUTS[1],
		FIELD_TRANSFORM_INPUTS[2],
		{.name = "Intensity", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}},
		{.name = "Drag", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 1, .whenOptions = 1u << 1},
	};

	inline constexpr GraphPortDefinition CONFORM_TO_SDF_INPUTS[] = {
		{.name = "Distance Field", .type = GraphPortType::Texture3D},
		FIELD_TRANSFORM_INPUTS[0],
		FIELD_TRANSFORM_INPUTS[1],
		FIELD_TRANSFORM_INPUTS[2],
		CONFORM_TO_SPHERE_INPUTS[2],
		CONFORM_TO_SPHERE_INPUTS[3],
		CONFORM_TO_SPHERE_INPUTS[4],
		CONFORM_TO_SPHERE_INPUTS[5],
	};

	inline constexpr GraphPortDefinition SAMPLE_SDF_INPUTS[] = {
		{.name = "Texture", .type = GraphPortType::Texture3D},
		{.name = "Center", .type = GraphPortType::Float3, .label = "Oriented Box Center", .space = GraphPortSpace::Matrix},
		{.name = "Angles", .type = GraphPortType::Float3, .label = "Oriented Box Angles"},
		{.name = "Size", .type = GraphPortType::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}, .label = "Oriented Box Size"},
		{.name = "Position", .type = GraphPortType::Float3, .space = GraphPortSpace::Position},
		{.name = "Mip Level", .type = GraphPortType::Float},
	};
	inline constexpr GraphPortDefinition SAMPLE_SDF_OUTPUTS[] = {{.name = "distance", .type = GraphPortType::Float}, {.name = "direction", .type = GraphPortType::Float3}};

	inline constexpr std::string_view ORIENT_MODES[] = {"FaceCameraPlane", "FaceCameraPosition", "LookAtPosition", "LookAtLine", "Advanced",
														"FixedAxis", "AlongVelocity", "CustomZ", "CustomY"};
	inline constexpr std::string_view ORIENT_MODE_NAMES[] = {"Face Camera Plane", "Face Camera Position", "Look At Position", "Look At Line", "Advanced",
															 "Fixed Axis", "Along Velocity", "Custom Z", "Custom Y"};
	inline constexpr std::string_view ORIENT_AXES[] = {"XY", "YZ", "ZX", "YX", "ZY", "XZ"};

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

	inline constexpr GraphSettingDefinition ORIENT_SETTINGS[] = {
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = ORIENT_MODES, .inGraph = true},
		{.key = "Axes", .kind = GraphSettingKind::Choice, .options = ORIENT_AXES, .defaultIndex = AxesZY, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << OrientAdvanced},
	};
	inline constexpr GraphPortDefinition AXES_OUTPUTS[] = {
		{.name = "axisX", .type = GraphPortType::Float3}, {.name = "axisY", .type = GraphPortType::Float3}, {.name = "axisZ", .type = GraphPortType::Float3}};

	inline constexpr std::string_view FLIPBOOK_MODES[] = {"FrameRate", "Cycles"};
	inline constexpr std::string_view FRAME_RATE_MODES[] = {"Constant", "Random", "OverLifetime", "BySpeed", "Custom"};
	inline constexpr std::string_view CYCLES_MODES[] = {"Constant", "Random", "RandomFullCycle"};
	inline constexpr std::string_view ANIMATION_RANGES[] = {"EntireFlipbook", "FlipbookRow", "FlipbookColumn", "StartEndFrames"};

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

	inline constexpr GraphSettingDefinition FLIPBOOK_PLAYER_SETTINGS[] = {
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = FLIPBOOK_MODES, .inGraph = true},
		{.key = "Frame Rate Mode", .kind = GraphSettingKind::Choice, .options = FRAME_RATE_MODES, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << ByFrameRate},
		{.key = "Cycles Mode", .kind = GraphSettingKind::Choice, .options = CYCLES_MODES, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << ByCycles},
		{.key = "Use Custom Range", .kind = GraphSettingKind::Toggle, .whenSetting = 0, .whenOptions = 1u << ByFrameRate},
		{.key = "Animation Range", .kind = GraphSettingKind::Choice, .options = ANIMATION_RANGES, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << ByCycles, .orSetting = 3, .orOptions = 1u << 1},
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
	inline constexpr GraphPortDefinition FLIPBOOK_PLAYER_INPUTS[] = {{.name = "Frame Rate", .type = GraphPortType::Float, .defaultValue = {24.0f, 0.0f, 0.0f, 0.0f}}};
	inline constexpr GraphPortDefinition TEX_INDEX_OUTPUT[] = {{.name = "texIndex", .type = GraphPortType::Float}};

	inline constexpr std::string_view MESH_PLACEMENTS[] = {"Vertex", "Edge", "Surface"};
	inline constexpr std::string_view SURFACE_COORDINATES[] = {"Barycentric", "Uniform"};
	inline constexpr std::string_view ADDRESSING_MODES[] = {"Wrap", "Clamp", "Mirror"};

	enum MeshPlacement : u32 { PlaceVertex = 0,
							   PlaceEdge,
							   PlaceSurface };
	enum SurfaceCoordinates : u32 { SurfaceBarycentric = 0,
									SurfaceUniform };
	enum AddressingMode : u32 { AddressWrap = 0,
								AddressClamp,
								AddressMirror };

	inline constexpr std::string_view MESH_OUTPUTS[] = {"Position", "Normal", "Tangent", "Bitangent", "Bitangent Sign", "Color", "Tex Coord 0"};
	enum MeshOutput : u32 { OutPosition = 0,
							OutNormal,
							OutTangent,
							OutBitangent,
							OutBitangentSign,
							OutColor,
							OutTexCoord0 };
	inline constexpr u32 MESH_OUTPUT_COUNT = static_cast<u32>(std::size(MESH_OUTPUTS));

	inline constexpr GraphSettingDefinition SAMPLE_MESH_SETTINGS[] = {
		{.key = "Output Position", .kind = GraphSettingKind::Toggle, .defaultIndex = 1},
		{.key = "Output Normal", .kind = GraphSettingKind::Toggle},
		{.key = "Output Tangent", .kind = GraphSettingKind::Toggle},
		{.key = "Output Bitangent", .kind = GraphSettingKind::Toggle},
		{.key = "Output Bitangent Sign", .kind = GraphSettingKind::Toggle},
		{.key = "Output Color", .kind = GraphSettingKind::Toggle, .defaultIndex = 1},
		{.key = "Output Tex Coord 0", .kind = GraphSettingKind::Toggle, .defaultIndex = 1},
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = ADDRESSING_MODES, .defaultIndex = AddressClamp, .inGraph = true},
		{.key = "Placement Mode", .kind = GraphSettingKind::Choice, .options = MESH_PLACEMENTS, .inGraph = true},
		{.key = "Surface Coordinates", .kind = GraphSettingKind::Choice, .options = SURFACE_COORDINATES, .defaultIndex = SurfaceUniform, .inGraph = true, .whenSetting = 8, .whenOptions = 1u << PlaceSurface},
	};
	enum SampleMeshSetting : u32 { SampleMeshModeKey = MESH_OUTPUT_COUNT,
								   SampleMeshPlacementKey,
								   SampleMeshSurfaceKey };

	inline constexpr std::string_view AXES_COMPOSITIONS[] = {"Set", "Blend"};
	inline constexpr GraphSettingDefinition POSITION_MESH_SETTINGS[] = {
		{.key = "Spawn Mode", .kind = GraphSettingKind::Choice, .options = SPAWN_MODES, .inGraph = true},
		{.key = "Placement Mode", .kind = GraphSettingKind::Choice, .options = MESH_PLACEMENTS, .inGraph = true},
		{.key = "Surface Coordinates", .kind = GraphSettingKind::Choice, .options = SURFACE_COORDINATES, .defaultIndex = SurfaceUniform, .inGraph = true, .whenSetting = 1, .whenOptions = 1u << PlaceSurface},
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = ADDRESSING_MODES, .defaultIndex = AddressClamp, .inGraph = true, .whenSetting = 0, .whenOptions = 1u << 1},
		{.key = "Apply Orientation", .kind = GraphSettingKind::Choice, .options = ORIENTATIONS, .defaultIndex = 1},
		{.key = "Composition Position", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_COMPOSITIONS},
		{.key = "Composition Direction", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_COMPOSITIONS, .whenSetting = 4, .whenOptions = DIRECTION_OPTIONS},
		{.key = "Composition Axes", .kind = GraphSettingKind::Choice, .options = AXES_COMPOSITIONS, .whenSetting = 4, .whenOptions = AXES_OPTIONS},
	};
	enum PositionMeshSetting : u32 { MeshSpawnModeKey = 0,
									 MeshPlacementKey,
									 MeshSurfaceKey,
									 MeshAddressingKey,
									 MeshOrientationKey,
									 MeshCompositionPositionKey,
									 MeshCompositionDirectionKey,
									 MeshCompositionAxesKey };

	inline constexpr GraphPortDefinition MESH_TRANSFORM_INPUTS[] = {
		{.name = "Transform Position", .type = GraphPortType::Float3, .space = GraphPortSpace::Matrix},
		{.name = "Transform Angles", .type = GraphPortType::Float3},
		{.name = "Transform Scale", .type = GraphPortType::Float3, .defaultValue = {1.0f, 1.0f, 1.0f, 0.0f}},
	};
	inline constexpr GraphPortDefinition MESH_INPUT[] = {{.name = "Mesh", .type = GraphPortType::Mesh}};
	inline constexpr GraphPortDefinition SAMPLE_MESH_INDEX_INPUTS[] = {MESH_INPUT[0], {.name = "Index", .type = GraphPortType::Float}};
	inline constexpr GraphPortDefinition SAMPLE_MESH_INDEX_OUTPUTS[] = {{.name = "Index", .type = GraphPortType::Float}};
	inline constexpr GraphPortDefinition MESH_COUNT_OUTPUTS[] = {{.name = "Count", .type = GraphPortType::Float}};

	inline constexpr std::string_view SEQUENTIAL_SHAPES[] = {"Line", "Circle", "ThreeDimensional"};
	inline constexpr std::string_view SEQUENTIAL_INDEX_MODES[] = {"ParticleID", "Custom"};
	enum SequentialShape : u32 { SequentialLine = 0,
								 SequentialCircle,
								 SequentialThreeDimensional };
	inline constexpr GraphSettingDefinition POSITION_SEQUENTIAL_SETTINGS[] = {
		{.key = "Composition Position", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_COMPOSITIONS, .defaultIndex = 1, .whenSetting = 5, .whenOptions = 1u << 1},
		{.key = "Composition Direction", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_COMPOSITIONS, .whenSetting = 5, .whenOptions = 1u << 1},
		{.key = "Composition Target Position", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_COMPOSITIONS, .defaultIndex = 1, .whenSetting = 6, .whenOptions = 1u << 1},
		{.key = "Shape", .kind = GraphSettingKind::Choice, .options = SEQUENTIAL_SHAPES},
		{.key = "Index", .kind = GraphSettingKind::Choice, .options = SEQUENTIAL_INDEX_MODES, .inGraph = true},
		{.key = "Write Position", .kind = GraphSettingKind::Toggle, .defaultIndex = 1, .inGraph = true},
		{.key = "Write Target Position", .kind = GraphSettingKind::Toggle, .inGraph = true},
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = ADDRESSING_MODES, .defaultIndex = AddressClamp, .inGraph = true},
	};
	enum PositionSequentialSetting : u32 { SequentialCompositionPositionKey = 0,
										   SequentialCompositionDirectionKey,
										   SequentialCompositionTargetKey,
										   SequentialShapeKey,
										   SequentialIndexKey,
										   SequentialWritePositionKey,
										   SequentialWriteTargetKey,
										   SequentialModeKey };

	inline constexpr std::string_view CAMERA_FADE_MODES[] = {"Color", "Alpha", "ColorAndAlpha"};
	enum CameraFadeMode : u32 { FadeColor = 0,
								FadeAlpha,
								FadeColorAndAlpha };
	inline constexpr GraphSettingDefinition CAMERA_FADE_SETTINGS[] = {
		{.key = "Fade Mode", .kind = GraphSettingKind::Choice, .options = CAMERA_FADE_MODES, .defaultIndex = FadeAlpha, .inGraph = true},
		{.key = "Cull When Faded", .kind = GraphSettingKind::Toggle, .defaultIndex = 1},
		{.key = "Affect Shadows", .kind = GraphSettingKind::Toggle},
	};
	inline constexpr GraphPortDefinition CAMERA_FADE_INPUTS[] = {
		{.name = "Faded Distance", .type = GraphPortType::Float, .defaultValue = {0.5f, 0.0f, 0.0f, 0.0f}},
		{.name = "Visible Distance", .type = GraphPortType::Float, .defaultValue = {2.0f, 0.0f, 0.0f, 0.0f}},
	};
	inline constexpr GraphPortDefinition CAMERA_FADE_OUTPUTS[] = {
		{.name = "color", .type = GraphPortType::Float3}, {.name = "alpha", .type = GraphPortType::Float}, {.name = "alive", .type = GraphPortType::Boolean}};

	enum TriggerMode : u32 { TriggerAlways = 0,
							 TriggerOverTime,
							 TriggerOverDistance,
							 TriggerOnDie,
							 TriggerOnCollide };
	inline constexpr std::string_view TRIGGER_MODE_NAMES[] = {"Always", "Over Time", "Over Distance", "On Die", "On Collide"};
	inline constexpr u32 TRIGGER_COUNT_MODES = (1u << TriggerAlways) | (1u << TriggerOnDie) | (1u << TriggerOnCollide);
	inline constexpr u32 TRIGGER_RATE_MODES = (1u << TriggerOverTime) | (1u << TriggerOverDistance);
	inline constexpr GraphSettingDefinition TRIGGER_EVENT_SETTINGS[] = {
		{.key = "Mode", .kind = GraphSettingKind::Choice, .options = GRAPH_EFFECT_TRIGGER_MODES, .inGraph = true},
		{.key = "Clamp To One", .kind = GraphSettingKind::Toggle, .defaultIndex = 1, .whenSetting = 0, .whenOptions = TRIGGER_RATE_MODES},
	};
	inline constexpr GraphPortDefinition TRIGGER_EVENT_INPUTS[] = {
		{.name = "Count", .type = GraphPortType::Float, .defaultValue = {1.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = TRIGGER_COUNT_MODES},
		{.name = "Rate", .type = GraphPortType::Float, .defaultValue = {10.0f, 0.0f, 0.0f, 0.0f}, .whenSetting = 0, .whenOptions = TRIGGER_RATE_MODES},
	};
	inline constexpr GraphPortDefinition TRIGGER_EVENT_OUTPUTS[] = {
		{.name = GRAPH_EFFECT_EVT, .type = GraphPortType::GPUEvent},
		{.name = GRAPH_EFFECT_RATE_COUNT, .type = GraphPortType::Float},
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
