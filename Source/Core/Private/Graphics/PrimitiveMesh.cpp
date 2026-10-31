#include "Shalltear/Core/Graphics/PrimitiveMesh.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <cmath>

namespace Shalltear {

	namespace {

		constexpr u32 SPHERE_SEGMENTS = 32;
		constexpr u32 SPHERE_RINGS = 16;
		constexpr u32 CYLINDER_SEGMENTS = 32;
		constexpr u32 CAPSULE_SEGMENTS = 32;
		constexpr u32 CAPSULE_CAP_RINGS = 8;
		constexpr u32 PLANE_SUBDIVISIONS = 10;
		constexpr f32 PLANE_SIZE = 10.0f;

		constexpr const char* DEFAULT_MATERIAL_PATH = "engine://Materials/Default.mat";

		void SetVertex(MeshVertex& vertex, const Vec3& position, const Vec3& normal, const Vec3& tangent, f32 u, f32 v) {
			vertex.position = position;
			vertex.normal = normal;
			vertex.tangent = tangent;
			vertex.tangentSign = 1.0f;
			vertex.u = u;
			vertex.v = v;
			vertex.color = Color32::White();
		}

		void PushTriangle(Vector<u32>& indices, u32 a, u32 b, u32 c) {
			indices.push_back(a);
			indices.push_back(b);
			indices.push_back(c);
		}

		void PushQuad(Vector<u32>& indices, u32 a, u32 b, u32 c, u32 d) {
			PushTriangle(indices, a, b, c);
			PushTriangle(indices, a, c, d);
		}

		void BuildCube(Vector<MeshVertex>& vertices, Vector<u32>& indices) {
			struct Face {
				Vec3 normal;
				Vec3 tangent;
			};
			const Face faces[6] = {
				{{0.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
				{{0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
				{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
				{{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
				{{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
				{{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
			};

			for (const Face& face : faces) {
				u32 base = static_cast<u32>(vertices.size());
				Vec3 center = face.normal * 0.5f;
				Vec3 bitangent = Vec3::Cross(face.normal, face.tangent);
				const Vec2 corners[4] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
				for (const Vec2& corner : corners) {
					Vec3 position = center + face.tangent * corner.x + bitangent * corner.y;
					MeshVertex vertex;
					SetVertex(vertex, position, face.normal, face.tangent, corner.x + 0.5f, 0.5f - corner.y);
					vertices.push_back(vertex);
				}
				PushQuad(indices, base, base + 1, base + 2, base + 3);
			}
		}

		void BuildSphere(Vector<MeshVertex>& vertices, Vector<u32>& indices) {
			for (u32 ring = 0; ring <= SPHERE_RINGS; ++ring) {
				f32 v = static_cast<f32>(ring) / static_cast<f32>(SPHERE_RINGS);
				f32 polar = v * Math::PI;
				f32 sinPolar = std::sin(polar);
				f32 cosPolar = std::cos(polar);

				for (u32 segment = 0; segment <= SPHERE_SEGMENTS; ++segment) {
					f32 u = static_cast<f32>(segment) / static_cast<f32>(SPHERE_SEGMENTS);
					f32 azimuth = u * Math::TAU;
					f32 sinAzimuth = std::sin(azimuth);
					f32 cosAzimuth = std::cos(azimuth);

					Vec3 normal{sinPolar * sinAzimuth, cosPolar, sinPolar * cosAzimuth};
					Vec3 tangent{cosAzimuth, 0.0f, -sinAzimuth};
					MeshVertex vertex;
					SetVertex(vertex, normal * 0.5f, normal, tangent, u, v);
					vertices.push_back(vertex);
				}
			}

			u32 stride = SPHERE_SEGMENTS + 1;
			for (u32 ring = 0; ring < SPHERE_RINGS; ++ring) {
				for (u32 segment = 0; segment < SPHERE_SEGMENTS; ++segment) {
					u32 current = ring * stride + segment;
					u32 next = current + stride;
					if (ring != 0) {
						PushTriangle(indices, current, next, current + 1);
					}
					if (ring != SPHERE_RINGS - 1) {
						PushTriangle(indices, current + 1, next, next + 1);
					}
				}
			}
		}

		void BuildPlane(Vector<MeshVertex>& vertices, Vector<u32>& indices) {
			f32 half = PLANE_SIZE * 0.5f;
			f32 step = PLANE_SIZE / static_cast<f32>(PLANE_SUBDIVISIONS);
			for (u32 z = 0; z <= PLANE_SUBDIVISIONS; ++z) {
				for (u32 x = 0; x <= PLANE_SUBDIVISIONS; ++x) {
					Vec3 position{-half + static_cast<f32>(x) * step, 0.0f, -half + static_cast<f32>(z) * step};
					MeshVertex vertex;
					SetVertex(vertex, position, Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f},
							  static_cast<f32>(x) / static_cast<f32>(PLANE_SUBDIVISIONS),
							  static_cast<f32>(z) / static_cast<f32>(PLANE_SUBDIVISIONS));
					vertices.push_back(vertex);
				}
			}

			u32 stride = PLANE_SUBDIVISIONS + 1;
			for (u32 z = 0; z < PLANE_SUBDIVISIONS; ++z) {
				for (u32 x = 0; x < PLANE_SUBDIVISIONS; ++x) {
					u32 corner = z * stride + x;
					PushQuad(indices, corner, corner + stride, corner + stride + 1, corner + 1);
				}
			}
		}

		void BuildQuad(Vector<MeshVertex>& vertices, Vector<u32>& indices) {
			const Vec2 corners[4] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
			for (const Vec2& corner : corners) {
				MeshVertex vertex;
				SetVertex(vertex, Vec3{corner.x, corner.y, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}, Vec3{-1.0f, 0.0f, 0.0f},
						  corner.x + 0.5f, 0.5f - corner.y);
				vertices.push_back(vertex);
			}
			PushQuad(indices, 0, 1, 2, 3);
		}

		void PushCap(Vector<MeshVertex>& vertices, Vector<u32>& indices, u32 segments, f32 radius, f32 y, f32 sign) {
			u32 center = static_cast<u32>(vertices.size());
			MeshVertex centerVertex;
			SetVertex(centerVertex, Vec3{0.0f, y, 0.0f}, Vec3{0.0f, sign, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, 0.5f, 0.5f);
			vertices.push_back(centerVertex);

			for (u32 segment = 0; segment <= segments; ++segment) {
				f32 angle = static_cast<f32>(segment) / static_cast<f32>(segments) * Math::TAU;
				f32 sinAngle = std::sin(angle);
				f32 cosAngle = std::cos(angle);
				MeshVertex vertex;
				SetVertex(vertex, Vec3{sinAngle * radius, y, cosAngle * radius}, Vec3{0.0f, sign, 0.0f},
						  Vec3{1.0f, 0.0f, 0.0f}, sinAngle * 0.5f + 0.5f, cosAngle * 0.5f + 0.5f);
				vertices.push_back(vertex);
			}

			for (u32 segment = 0; segment < segments; ++segment) {
				u32 a = center + 1 + segment;
				u32 b = center + 2 + segment;
				if (sign > 0.0f) {
					PushTriangle(indices, center, a, b);
				} else {
					PushTriangle(indices, center, b, a);
				}
			}
		}

		void BuildCylinder(Vector<MeshVertex>& vertices, Vector<u32>& indices) {
			constexpr f32 RADIUS = 0.5f;
			constexpr f32 HALF_HEIGHT = 1.0f;

			u32 base = static_cast<u32>(vertices.size());
			for (u32 segment = 0; segment <= CYLINDER_SEGMENTS; ++segment) {
				f32 u = static_cast<f32>(segment) / static_cast<f32>(CYLINDER_SEGMENTS);
				f32 angle = u * Math::TAU;
				f32 sinAngle = std::sin(angle);
				f32 cosAngle = std::cos(angle);
				Vec3 normal{sinAngle, 0.0f, cosAngle};
				Vec3 tangent{cosAngle, 0.0f, -sinAngle};

				MeshVertex top;
				SetVertex(top, Vec3{normal.x * RADIUS, HALF_HEIGHT, normal.z * RADIUS}, normal, tangent, u, 0.0f);
				vertices.push_back(top);
				MeshVertex bottom;
				SetVertex(bottom, Vec3{normal.x * RADIUS, -HALF_HEIGHT, normal.z * RADIUS}, normal, tangent, u, 1.0f);
				vertices.push_back(bottom);
			}
			for (u32 segment = 0; segment < CYLINDER_SEGMENTS; ++segment) {
				u32 corner = base + segment * 2;
				PushQuad(indices, corner, corner + 1, corner + 3, corner + 2);
			}

			PushCap(vertices, indices, CYLINDER_SEGMENTS, RADIUS, HALF_HEIGHT, 1.0f);
			PushCap(vertices, indices, CYLINDER_SEGMENTS, RADIUS, -HALF_HEIGHT, -1.0f);
		}

		void BuildCapsule(Vector<MeshVertex>& vertices, Vector<u32>& indices) {
			constexpr f32 RADIUS = 0.5f;
			constexpr f32 HALF_MID = 0.5f;

			u32 ringCount = CAPSULE_CAP_RINGS * 2 + 2;
			for (u32 ring = 0; ring < ringCount; ++ring) {
				bool topHalf = ring <= CAPSULE_CAP_RINGS;
				u32 capRing = topHalf ? ring : ring - CAPSULE_CAP_RINGS - 1;
				f32 polar = (topHalf ? 0.0f : Math::PI * 0.5f) +
							static_cast<f32>(capRing) / static_cast<f32>(CAPSULE_CAP_RINGS) * Math::PI * 0.5f;
				f32 sinPolar = std::sin(polar);
				f32 cosPolar = std::cos(polar);
				f32 yOffset = topHalf ? HALF_MID : -HALF_MID;

				for (u32 segment = 0; segment <= CAPSULE_SEGMENTS; ++segment) {
					f32 u = static_cast<f32>(segment) / static_cast<f32>(CAPSULE_SEGMENTS);
					f32 azimuth = u * Math::TAU;
					f32 sinAzimuth = std::sin(azimuth);
					f32 cosAzimuth = std::cos(azimuth);

					Vec3 normal{sinPolar * sinAzimuth, cosPolar, sinPolar * cosAzimuth};
					Vec3 position = normal * RADIUS;
					position.y += yOffset;
					Vec3 tangent{cosAzimuth, 0.0f, -sinAzimuth};

					MeshVertex vertex;
					SetVertex(vertex, position, normal, tangent, u,
							  static_cast<f32>(ring) / static_cast<f32>(ringCount - 1));
					vertices.push_back(vertex);
				}
			}

			u32 stride = CAPSULE_SEGMENTS + 1;
			for (u32 ring = 0; ring + 1 < ringCount; ++ring) {
				for (u32 segment = 0; segment < CAPSULE_SEGMENTS; ++segment) {
					u32 current = ring * stride + segment;
					u32 next = current + stride;
					if (ring != 0) {
						PushTriangle(indices, current, next, current + 1);
					}
					if (ring + 2 != ringCount) {
						PushTriangle(indices, current + 1, next, next + 1);
					}
				}
			}
		}

	} // anonymous namespace

	std::string_view PrimitiveShapeName(PrimitiveShape shape) {
		switch (shape) {
		case PrimitiveShape::Cube:
			return "Cube";
		case PrimitiveShape::Sphere:
			return "Sphere";
		case PrimitiveShape::Plane:
			return "Plane";
		case PrimitiveShape::Quad:
			return "Quad";
		case PrimitiveShape::Cylinder:
			return "Cylinder";
		case PrimitiveShape::Capsule:
			return "Capsule";
		default:
			return {};
		}
	}

	bool ParsePrimitiveShape(std::string_view name, PrimitiveShape& outShape) {
		for (u8 i = 0; i < static_cast<u8>(PrimitiveShape::Count); ++i) {
			PrimitiveShape shape = static_cast<PrimitiveShape>(i);
			if (PrimitiveShapeName(shape) == name) {
				outShape = shape;
				return true;
			}
		}
		return false;
	}

	bool BuildPrimitiveMesh(PrimitiveShape shape, MeshAsset& out) {
		if (shape >= PrimitiveShape::Count) {
			return false;
		}

		out.vertices.clear();
		out.indices.clear();
		out.submeshes.clear();

		switch (shape) {
		case PrimitiveShape::Cube:
			BuildCube(out.vertices, out.indices);
			break;
		case PrimitiveShape::Sphere:
			BuildSphere(out.vertices, out.indices);
			break;
		case PrimitiveShape::Plane:
			BuildPlane(out.vertices, out.indices);
			break;
		case PrimitiveShape::Quad:
			BuildQuad(out.vertices, out.indices);
			break;
		case PrimitiveShape::Cylinder:
			BuildCylinder(out.vertices, out.indices);
			break;
		case PrimitiveShape::Capsule:
			BuildCapsule(out.vertices, out.indices);
			break;
		default:
			return false;
		}

		out.bounds = AABB::Empty();
		for (const MeshVertex& vertex : out.vertices) {
			out.bounds.Encapsulate(vertex.position);
		}

		SubMesh submesh;
		submesh.firstIndex = 0;
		submesh.indexCount = static_cast<u32>(out.indices.size());
		submesh.materialSlot = 0;
		submesh.bounds = out.bounds;
		out.submeshes.push_back(submesh);
		return true;
	}

	str GetPrimitiveMeshPath(PrimitiveShape shape) {
		std::string_view name = PrimitiveShapeName(shape);
		if (name.empty()) {
			return {};
		}
		return str("engine://Meshes/") + str(name) + ".primitive";
	}

} // namespace Shalltear
