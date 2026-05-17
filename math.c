#include <math.h>

#define PI 3.14159265359

typedef struct {
    float x, y, z, w;
} vec4;

typedef struct vec3 {
    float x, y, z;
} vec3;

typedef struct vec2 {
    float x, y;
} vec2;

typedef struct mat4 {
    union {
        struct {
            // glsl and slang use column major
            float m11, m21, m31, m41, // column 1
                  m12, m22, m32, m42, // column 2
                  m13, m23, m33, m43, // ...
                  m14, m24, m34, m44;
        };
        struct {
            vec4 c1, c2, c3, c4;
        };
        float raw[16];
    };
} mat4;

typedef struct {
    float w, i, j, k;
} quat;

// vector ops

vec3 vec3_zero    = {0, 0, 0};
vec3 vec3_up      = {0, 1, 0};
vec3 vec3_forward = {0, 0,-1};
vec3 vec3_right   = {1, 0, 0};

void vec3_print(vec3 v, int(*fp)(const char*, ...)) {
    fp("[%f, %f, %f]\n", v.x, v.y, v.z);
}

vec2 vec2_add(vec2 a, vec2 b) { return (vec2){a.x + b.x, a.y + b.y}; }
vec3 vec3_add(vec3 a, vec3 b) { return (vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
vec4 vec4_add(vec4 a, vec4 b) { return (vec4){a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }

vec2 vec2_sub(vec2 a, vec2 b) { return (vec2){a.x - b.x, a.y - b.y}; }
vec3 vec3_sub(vec3 a, vec3 b) { return (vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
vec4 vec4_sub(vec4 a, vec4 b) { return (vec4){a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }

vec2 vec2_scale(vec2 a, float t) { return (vec2){a.x * t, a.y * t}; }
vec3 vec3_scale(vec3 a, float t) { return (vec3){a.x * t, a.y * t, a.z * t}; }
vec4 vec4_scale(vec4 a, float t) { return (vec4){a.x * t, a.y * t, a.z * t, a.w * t}; }

vec3 vec3_neg(vec3 a) { return (vec3){-a.x, -a.y, -a.z}; }

float vec3_dot(vec3 a, vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
float vec4_dot(vec4 a, vec4 b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }

float vec2_sq_magnitude(vec2 a) { return a.x * a.x + a.y * a.y; }
float vec3_sq_magnitude(vec3 a) { return a.x * a.x + a.y * a.y + a.z * a.z; }
#define vec2_sq_dist(a, b) (vec2_sq_magnitude(vec2_sub((a), (b))))
#define vec3_sq_dist(a, b) (vec3_sq_magnitude(vec3_sub((a), (b))))

float vec2_magnitude(vec2 a) {
    float M = fmaxf(fabs(a.x), fabs(a.y));
    if (M == 0.0f) return 0.0f;
    vec2 u = vec2_scale(a, 1/M);
    return M * sqrtf(u.x * u.x + u.y * u.y);
}

float vec3_magnitude(vec3 a) {
    float M = fmaxf(fmaxf(fabs(a.x), fabs(a.y)), fabs(a.z));
    if (M == 0.0f) return 0.0f;
    vec3 u = vec3_scale(a, 1/M);
    return M * sqrtf(u.x * u.x + u.y * u.y + u.z * u.z);
}

vec2 vec2_normalized(vec2 a) { return vec2_scale(a, 1.0f/vec2_magnitude(a)); }
vec3 vec3_normalized(vec3 a) { return vec3_scale(a, 1.0f/vec3_magnitude(a)); }

bool vec3_is_zero(vec3 a) {
    return a.x == 0.f && a.y == 0.f && a.z == 0.f;
}

vec3 vec3_cross(vec3 a, vec3 b) {
    return (vec3){
        a.y * b.z - a.z * b.y, 
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

bool vec3_eq(vec3 a, vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

vec3 vec3_lerp(vec3 a, vec3 b, float t) {
    assert(0 <= t && t <= 1.0f);
    if (vec3_eq(a,b)) return a;
    vec3 r = vec3_normalized(vec3_sub(b, a));
    return vec3_add(a, vec3_scale(r, t));
}


// Matrix Ops

const mat4 mat4_identity = { .m11 = 1.0f, .m22 = 1.0f, .m33 = 1, .m44 = 1 };

void mat4_print(mat4* m, int(*fp)(const char*, ...)) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            fp("%f ", m->raw[j * 4 + i]); // index column major
        }
        fp("\n");
    }
}


// Multiply a mat4 by a column vector
vec4 mat4_apply(const mat4* m, vec4 v) {
    vec4 r;

    r.x = m->m11*v.x + m->m12*v.y + m->m13*v.z + m->m14*v.w;
    r.y = m->m21*v.x + m->m22*v.y + m->m23*v.z + m->m24*v.w;
    r.z = m->m31*v.x + m->m32*v.y + m->m33*v.z + m->m34*v.w;
    r.w = m->m41*v.x + m->m42*v.y + m->m43*v.z + m->m44*v.w;

    return r;
}

// clang 18.1 is confirmed to perform rvo here

mat4 mat4_transposed(const mat4* A) {
    mat4 out;
    out.m11 = A->m11; out.m21 = A->m12; out.m31 = A->m13; out.m41 = A->m14;
    out.m12 = A->m21; out.m22 = A->m22; out.m32 = A->m23; out.m42 = A->m24;
    out.m13 = A->m31; out.m23 = A->m32; out.m33 = A->m33; out.m43 = A->m34;
    out.m14 = A->m41; out.m24 = A->m42; out.m34 = A->m43; out.m44 = A->m44;
    return out;
}

mat4 mat4_mul(const mat4* A, const mat4* B) {
    mat4 A_T = mat4_transposed(A);
    mat4 C;
    C.m11 = vec4_dot(A_T.c1, B->c1); C.m21 = vec4_dot(A_T.c2, B->c1);
    C.m12 = vec4_dot(A_T.c1, B->c2); C.m22 = vec4_dot(A_T.c2, B->c2);
    C.m13 = vec4_dot(A_T.c1, B->c3); C.m23 = vec4_dot(A_T.c2, B->c3);
    C.m14 = vec4_dot(A_T.c1, B->c4); C.m24 = vec4_dot(A_T.c2, B->c4);

    C.m31 = vec4_dot(A_T.c3, B->c1); C.m41 = vec4_dot(A_T.c4, B->c1);
    C.m32 = vec4_dot(A_T.c3, B->c2); C.m42 = vec4_dot(A_T.c4, B->c2);
    C.m33 = vec4_dot(A_T.c3, B->c3); C.m43 = vec4_dot(A_T.c4, B->c3);
    C.m34 = vec4_dot(A_T.c3, B->c4); C.m44 = vec4_dot(A_T.c4, B->c4);
    return C;
}

void mat4_inverse_rigid(mat4* out, const mat4* m)
{
    // Extract rotation (upper-left 3x3)
    float r00 = m->m11, r01 = m->m12, r02 = m->m13;
    float r10 = m->m21, r11 = m->m22, r12 = m->m23;
    float r20 = m->m31, r21 = m->m32, r22 = m->m33;

    // Extract translation
    float tx = m->m14;
    float ty = m->m24;
    float tz = m->m34;

    // Transpose rotation (R^T)
    out->m11 = r00; out->m12 = r10; out->m13 = r20;
    out->m21 = r01; out->m22 = r11; out->m23 = r21;
    out->m31 = r02; out->m32 = r12; out->m33 = r22;

    // New translation = -R^T * T
    out->m14 = -(out->m11 * tx + out->m12 * ty + out->m13 * tz);
    out->m24 = -(out->m21 * tx + out->m22 * ty + out->m23 * tz);
    out->m34 = -(out->m31 * tx + out->m32 * ty + out->m33 * tz);

    // Last row
    out->m41 = 0.0f;
    out->m42 = 0.0f;
    out->m43 = 0.0f;
    out->m44 = 1.0f;
}

//typedef float mat4 __attribute__((matrix_type(4,4)));
//
//static inline mat4 mat4_mul(mat4 a, mat4 b, mat4 r) {
//    // initialize all elements to 0
//    for(int i=0;i<4;i++)
//        for(int j=0;j<4;j++)
//            r[i][j] = 0.0f;
//
//    for(int i=0;i<4;i++)
//        for(int j=0;j<4;j++)
//            for(int k=0;k<4;k++)
//                r[i][j] += a[i][k] * b[k][j];
//    return r;
//}

//// Credit to Andrew Kay at (public domain at time of writing)
//// https://andrewkay.name/blog/post/efficiently-approximating-tan-x/
//// for a good tan approximation. Keep in mind to pass values [0, pi/2]
//float TA3 (float x)
//{
//  static const float pisqby4 = 2.4674011002723397f;
//  static const float adjpisqby4 = 2.471688400562703f;
//  static const float adj1minus8bypisq = 0.189759681063053f;
//  float xsq = x * x;
//  return x * (adjpisqby4 - adj1minus8bypisq * xsq) / (pisqby4 - xsq);
//}

// TODO: make more generic to handle asymmetric frustums, eg shadow maps
// assumes m is zero initialized
mat4 mat4_perspective(float fov_rads, float aspect, float znear, float zfar) {
    mat4 m = {0};
    float t = znear * tanf(fov_rads / 2.0f); // top is down
    float r = t * aspect; 

    // column-major layout
    m.m11 = znear / r;    // 2n/(r-l) = 2n/2r since l = -r
    m.m22 = -znear / t;        // same as above (negated)
    m.m33 = zfar / (znear - zfar);  // map z to [0,1] (negated)
    m.m43 = -1.0f;       // perspective divide (negated)
    m.m34 = (zfar * znear) / (znear - zfar); // translation
    // m44 = 0.0f already
    return m;
}


//void mat4_look_at(mat4* m, vec3 eye, vec3 center, vec3 up) {
//    vec3 f = vec3_normalized(vec3_sub(center, eye));   // forward
//    vec3 r = vec3_normalized(vec3_cross(f, up));      // right
//    vec3 u = vec3_cross(r,f);                       // orthonormal up
//
//    // zero out matrix
//    for (int i = 0; i < 16; i++) m->raw[i] = 0.0f;
//
//    // column 1 (right)
//    m->c1 = r;
//    m->c2 = u;
//    m->c3 = f;
//    m->c4 = eye;
//    // column 4 (translation)
//    //m->m14 = vec3_dot(s, eye);
//    //m->m24 = vec3_dot(u, eye);
//    //m->m34 = vec3_dot(f, eye);
//    m->m44 = 1.0f;
//}
//
mat4 mat4_rotation(float angle, vec3 axis) {
    vec3 u = vec3_normalized(axis);

    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;
    mat4 m;

    // Column-major
    m.m11 = t*u.x*u.x + c;
    m.m12 = t*u.x*u.y + s*u.z;
    m.m13 = t*u.x*u.z - s*u.y;
    m.m14 = 0.0f;

    m.m21 = t*u.x*u.y - s*u.z;
    m.m22 = t*u.y*u.y + c;
    m.m23 = t*u.y*u.z + s*u.x;
    m.m24 = 0.0f;

    m.m31 = t*u.x*u.z + s*u.y;
    m.m32 = t*u.y*u.z - s*u.x;
    m.m33 = t*u.z*u.z + c;
    m.m34 = 0.0f;

    m.m41 = 0.0f;
    m.m42 = 0.0f;
    m.m43 = 0.0f;
    m.m44 = 1.0f;

    return m;
}

// Quaternion Ops

void quat_print(quat q, int(*fp)(const char*, ...)) {
    fp("[%f, %f, %f, %f]\n", q.w, q.i, q.j, q.k);
}


mat4 quat_to_mat4(quat q) {
    float ii = q.i * q.i, jj = q.j * q.j, kk = q.k * q.k;
    float ij = q.i * q.j, ik = q.i * q.k, jk = q.j * q.k;
    float wi = q.w * q.i, wj = q.w * q.j, wk = q.w * q.k;
    return (mat4){
        1 - 2*(jj + kk),  2*(ij + wk),      2*(ik - wj),      0,
        2*(ij - wk),      1 - 2*(ii + kk),  2*(jk + wi),      0,
        2*(ik + wj),      2*(jk - wi),      1 - 2*(ii + jj),  0,
        0,                0,                0,                  1
    };
}

quat quat_mul(quat a, quat b) {
    return (quat){
        a.w * b.w - a.i * b.i - a.j * b.j - a.k * b.k,
        a.w * b.i + a.i * b.w + a.j * b.k - a.k * b.j,
        a.w * b.j + a.j * b.w + a.k * b.i - a.i * b.k,
        a.w * b.k + a.k * b.w + a.i * b.j - a.j * b.i
    };
}

quat quat_conjugate(quat a) {
    return (quat) { a.w, -a.i, -a.j, -a.k };
}

quat quat_rotate_around(vec3 axis, float ang) {
    float theta = ang/2; 
    float sin_theta = sinf(theta);
    return (quat) {cosf(theta), axis.x * sin_theta, axis.y * sin_theta, axis.z * sin_theta};
}

vec3 quat_rotate_vec3(quat q, vec3 v) {
    quat q_inv = quat_conjugate(q);
    quat q_v = (quat) { 0.0f, v.x, v.y, v.z };
    quat rotated = quat_mul(quat_mul(q, q_v), q_inv);
    return (vec3) {rotated.i, rotated.j, rotated.k};
}

