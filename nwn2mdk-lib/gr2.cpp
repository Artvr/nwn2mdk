#include <cmath>

#include "gr2.h"

static_assert(sizeof(Vector3<float>) == 3 * 4, "");
static_assert(sizeof(Vector4<float>) == 4 * 4, "");
static_assert(sizeof(GR2_art_tool_info) == 64, "");
static_assert(sizeof(GR2_transform) == 68, "");
static_assert(sizeof(GR2_property_key) == 8 * 4, "");
static_assert(sizeof(GR2_extended_data) == 2 * 4, "");

// 1.4142135 = sqrt(2)
// 0.70710677 = 1/sqrt(2)
// 0.53033006 = (1/sqrt(2) + 1/sqrt(8))/2
// 0.35355338 = 1/sqrt(8)
// 0.17677669 = 1/sqrt(32)
// 0.088388346 = 1/sqrt(128)
static const float scale_table[] = {
    1.4142135f,   0.70710677f,  0.35355338f,  0.35355338f,
    0.35355338f,  0.17677669f,  0.17677669f,  0.17677669f,
    -1.4142135f,  -0.70710677f, -0.35355338f, -0.35355338f,
    -0.35355338f, -0.17677669f, -0.17677669f, -0.17677669f};

static const float offset_table[] = {
    -0.70710677f, -0.35355338f, -0.53033006f,  -0.17677669f,
    0.17677669f,  -0.17677669f, -0.088388346f, 0.0f,
    0.70710677f,  0.35355338f,  0.53033006f,   0.17677669f,
    -0.17677669f, 0.17677669f,  0.088388346f,  -0.0f};

using namespace std;

static void compute_offsets(float offsets[4], uint16_t selectors[4])
{
	for (int i = 0; i < 4; ++i)
		offsets[i] = offset_table[selectors[i]];
}

static void compute_selectors(uint16_t selectors[4],
                              uint16_t scale_offset_table_entries)
{
	selectors[0] = (scale_offset_table_entries >> 0) & 0x0F;
	selectors[1] = (scale_offset_table_entries >> 4) & 0x0F;
	selectors[2] = (scale_offset_table_entries >> 8) & 0x0F;
	selectors[3] = (scale_offset_table_entries >> 12) & 0x0F;
}

static Vector4<float> decode_D4nK16uC15u(uint16_t a, uint16_t b, uint16_t c,
                                         float scales[], float offsets[])
{
	// A quaternion (4 components) is encoded in three values (a, b, c)
	//
	// a: 15 ... 1 0 | b: 15 ... 1 0 | c: 15 ... 1 0
	//    g    da        s1a   db        s1b   dc
	//
	// da, db, dc: 3 components of the quaternion
	// g: sign flag for 4th component of the quaternion (dd)
	// s1a, s1b: swizzle

	int s1a = (b & 0x8000) >> 14;
	int s1b = c >> 15;
	int swizzle1 = s1a | s1b;

	// swizzle_n = swizzle_{n-1} mod 4
	int swizzle2 = (swizzle1 + 1) & 3;
	int swizzle3 = (swizzle2 + 1) & 3;
	int swizzle4 = (swizzle3 + 1) & 3;

	float da = (a & 0x7fff) * scales[swizzle2] + offsets[swizzle2];
	float db = (b & 0x7fff) * scales[swizzle3] + offsets[swizzle3];
	float dc = (c & 0x7fff) * scales[swizzle4] + offsets[swizzle4];

	// Reconstruct dd considering quaternion is unit length
	float dd = sqrtf(1 - (da * da + db * db + dc * dc));
	if ((a & 0x8000) != 0)
		dd = -dd;

	Vector4<float> quat;
	quat[swizzle2] = da;
	quat[swizzle3] = db;
	quat[swizzle4] = dc;
	quat[swizzle1] = dd;
	return quat;
}

static Vector4<float> decode_D4nK8uC7u(uint8_t a, uint8_t b, uint8_t c,
                                       float scales[], float offsets[])
{
	// A quaternion (4 components) is encoded in three values (a, b, c)
	//
	// a: 7 ... 1 0 | b: 7 ... 1 0 | c: 7 ... 1 0
	//    g   da       s1a   db       s1b   dc
	//
	// da, db, dc: 3 of 4 components
	// g: sign flag for 4th component (dd)
	// s1a, s1b: swizzle

	int s1a = (b & 0x80) >> 6;
	int s1b = (c & 0x80) >> 7;
	int swizzle1 = s1a | s1b;

	// swizzle_n = swizzle_{n-1} mod 4
	int swizzle2 = (swizzle1 + 1) & 3;
	int swizzle3 = (swizzle2 + 1) & 3;
	int swizzle4 = (swizzle3 + 1) & 3;

	float da = (a & 0x7f) * scales[swizzle2] + offsets[swizzle2];
	float db = (b & 0x7f) * scales[swizzle3] + offsets[swizzle3];
	float dc = (c & 0x7f) * scales[swizzle4] + offsets[swizzle4];

	// Reconstruct dd considering quaternion is unit length
	float dd = sqrtf(1 - (da * da + db * db + dc * dc));
	if ((a & 0x80) != 0)
		dd = -dd;

	Vector4<float> quat;
	quat[swizzle2] = da;
	quat[swizzle3] = db;
	quat[swizzle4] = dc;
	quat[swizzle1] = dd;
	return quat;
}

const char* curve_format_to_str(uint8_t format)
{
	const char* s[] = {"DaKeyframes32f", "DaK32fC32f",    "DaIdentity",
	                   "DaConstant32f",  "D3Constant32f", "D4Constant32f",
	                   "DaK16uC16u",     "DaK8uC8u",      "D4nK16uC15u",
	                   "D4nK8uC7u",      "D3K16uC16u",    "D3K8uC8u"};

	if (format >= sizeof(s) / sizeof(char*))
		return "UNKNOWN";
	else
		return s[format];
}

const char* property_type_to_str(GR2_property_type type)
{
	const char* s[] = {"NONE", // 0
	                   "INLINE",
	                   "REFERENCE",
	                   "POINTER",
	                   "ARRAY OF REFERENCES",
	                   "VARIANT REFERENCE",
	                   "UNKNOWN",
	                   "REF TO VARIANT ARRAY",
	                   "TEXT",
	                   "TRANSFORM",
	                   "REAL32", // 10
	                   "UNKNOWN",
	                   "UINT8",
	                   "UNKNOWN",
	                   "UNKNOWN",
	                   "INT16",
	                   "UINT16",
	                   "UNKNOWN",
	                   "UNKNOWN",
	                   "INT32",
	                   "UNKNOWN", // 20
	                   "UNKNOWN",
	                   "UNKNOWN",
	                   "UNKNOWN"};

	return s[type];
}

GR2_D3K8uC8u_view::GR2_D3K8uC8u_view(GR2_curve_data_D3K8uC8u& data)
{
	int knots_count = data.knots_controls_count / 4;

	float one_over_knot_scale;
	unsigned tmp = (unsigned)data.one_over_knot_scale_trunc << 16;
	memcpy(&one_over_knot_scale, &tmp, sizeof(tmp));

	for (int i = 0; i < knots_count; ++i) {
		encoded_knots_.push_back(data.knots_controls[i]);
		knots_.push_back(data.knots_controls[i] / one_over_knot_scale);
	}

	int controls_count = data.knots_controls_count - knots_count;
	uint8_t* controls = data.knots_controls + knots_count;

	for (int i = 0; i < controls_count; i += 3) {
		uint8_t a = controls[i + 0];
		uint8_t b = controls[i + 1];
		uint8_t c = controls[i + 2];
		encoded_controls_.emplace_back(a, b, c);

		float x = a * data.control_scales[0] + data.control_offsets[0];
		float y = b * data.control_scales[1] + data.control_offsets[1];
		float z = c * data.control_scales[2] + data.control_offsets[2];
		controls_.emplace_back(x, y, z);
	}
}

const std::vector<uint8_t>& GR2_D3K8uC8u_view::encoded_knots() const
{
	return encoded_knots_;
}

const std::vector<float>& GR2_D3K8uC8u_view::knots() const
{
	return knots_;
}

const std::vector<Vector3<uint8_t>>& GR2_D3K8uC8u_view::encoded_controls() const
{
	return encoded_controls_;
}

const std::vector<Vector3<float>>& GR2_D3K8uC8u_view::controls() const
{
	return controls_;
}

GR2_D4nK16uC15u_view::GR2_D4nK16uC15u_view(GR2_curve_data_D4nK16uC15u& data)
{
	int knots_count = data.knots_controls_count / 4;
	for (int i = 0; i < knots_count; ++i) {
		encoded_knots_.push_back(data.knots_controls[i]);
		knots_.push_back(data.knots_controls[i] /
		                 data.one_over_knot_scale);
	}

	compute_selectors(selectors, data.scale_offset_table_entries);

	for (int i = 0; i < 4; ++i)
		scales[i] = scale_table[selectors[i]] * 0.000030518509f;

	compute_offsets(offsets, selectors);

	uint16_t* controls = data.knots_controls + knots_count;
	int controls_count = data.knots_controls_count - knots_count;

	for (int i = 0; i < controls_count; i += 3) {
		uint16_t a = controls[i + 0];
		uint16_t b = controls[i + 1];
		uint16_t c = controls[i + 2];
		encoded_controls_.emplace_back(a, b, c);
		controls_.push_back(
		    decode_D4nK16uC15u(a, b, c, scales, offsets));
	}
}

const std::vector<uint16_t>& GR2_D4nK16uC15u_view::encoded_knots() const
{
	return encoded_knots_;
}

const std::vector<float>& GR2_D4nK16uC15u_view::knots() const
{
	return knots_;
}

const std::vector<Vector3<uint16_t>>&
GR2_D4nK16uC15u_view::encoded_controls() const
{
	return encoded_controls_;
}

const std::vector<Vector4<float>>& GR2_D4nK16uC15u_view::controls() const
{
	return controls_;
}

GR2_D4nK8uC7u_view::GR2_D4nK8uC7u_view(GR2_curve_data_D4nK8uC7u& data)
{
	int knots_count = data.knots_controls_count / 4;
	for (int i = 0; i < knots_count; ++i) {
		encoded_knots_.push_back(data.knots_controls[i]);
		knots_.push_back(data.knots_controls[i] /
		                 data.one_over_knot_scale);
	}

	compute_selectors(selectors, data.scale_offset_table_entries);

	for (int i = 0; i < 4; ++i)
		scales[i] = scale_table[selectors[i]] * 0.0078740157f;

	compute_offsets(offsets, selectors);

	uint8_t* controls = data.knots_controls + knots_count;
	int controls_count = data.knots_controls_count - knots_count;

	for (int i = 0; i < controls_count; i += 3) {
		uint8_t a = controls[i + 0];
		uint8_t b = controls[i + 1];
		uint8_t c = controls[i + 2];
		encoded_controls_.emplace_back(a, b, c);
		controls_.emplace_back(
		    decode_D4nK8uC7u(a, b, c, scales, offsets));
	}
}

const std::vector<uint8_t>& GR2_D4nK8uC7u_view::encoded_knots() const
{
	return encoded_knots_;
}

const std::vector<float>& GR2_D4nK8uC7u_view::knots() const
{
	return knots_;
}

const std::vector<Vector3<uint8_t>>&
GR2_D4nK8uC7u_view::encoded_controls() const
{
	return encoded_controls_;
}

const std::vector<Vector4<float>>& GR2_D4nK8uC7u_view::controls() const
{
	return controls_;
}