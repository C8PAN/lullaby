/*
Copyright 2017 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS-IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef LULLABY_SYSTEMS_RENDER_RENDER_SYSTEM_H_
#define LULLABY_SYSTEMS_RENDER_RENDER_SYSTEM_H_

#include <memory>
#include <string>

#include "fplbase/render_state.h"
#include "lullaby/modules/ecs/system.h"
#include "lullaby/modules/render/image_data.h"
#include "lullaby/modules/render/material_info.h"
#include "lullaby/modules/render/mesh_data.h"
#include "lullaby/modules/render/mesh_util.h"
#include "lullaby/modules/render/render_view.h"
#include "lullaby/modules/render/vertex.h"
#include "lullaby/systems/render/mesh.h"
#include "lullaby/systems/render/render_target.h"
#include "lullaby/systems/render/render_types.h"
#include "lullaby/systems/render/shader.h"
#include "lullaby/systems/render/texture.h"
#include "lullaby/systems/render/uniform.h"
#include "lullaby/util/bits.h"
#include "lullaby/generated/material_def_generated.h"
#include "lullaby/generated/render_def_generated.h"
#include "lullaby/generated/render_pass_def_generated.h"
#include "lullaby/generated/render_target_def_generated.h"
#include "mathfu/glsl_mappings.h"

namespace lull {

class Font;
using FontPtr = std::shared_ptr<Font>;

// Explicitly set the minimum and maximum mip levels when LoadTexture is called
// with create_mips = false.  Useful when running on ANGLE as a GL backend.
// TODO(b/69430678): Fix the bug inside of ANGLE and eliminate this.
const HashValue kExplicitTextureMipLevels =
    Hash("lull.Render.ExplicitTextureMipLevels");

class RenderSystemImpl;

class Font;
using FontPtr = std::shared_ptr<Font>;

// The RenderSystem can be used to draw Entities using the GPU.
class RenderSystem : public System {
 public:
  struct InitParams {
    InitParams() : native_window(nullptr), enable_stereo_multiview(false) {}
    void* native_window;
    bool enable_stereo_multiview;
  };

  // A special pass id that allows the RenderSystem to use whatever pass it
  // considers to be the "default" pass.  Users can change this "default" pass
  // explicitly by calling RenderSystem::SetDefaultRenderPass(HashValue pass);
  static const HashValue kDefaultPass = 0xffffffff;

  explicit RenderSystem(Registry* registry,
                        const InitParams& init_params = InitParams());
  ~RenderSystem() override;

  /// Initializes inter-system dependencies.
  void Initialize() override;

  /// Prepares the render system to render a new frame. This needs to be called
  /// at the beginning of the frame before issuing any render calls. This
  /// function handles clearing the frame buffer and readying up for drawing to
  /// the display.
  void BeginFrame();

  /// Performs post-frame processing. This needs to be called at the end of the
  /// frame before submitting starting to draw another frame or calling
  /// BeginFrame a second time.
  void EndFrame();

  /// Sets the Render System to begin rendering. This will also attempt to swap
  /// the render data to the latest data submitted via |Submit()|. Must be
  /// called before any render calls are made and finished by calling
  /// |EndRendering()|.
  void BeginRendering();

  /// Sets the Render System to finish the render sequence. This also frees the
  /// render buffer data for writing new data. Must be preceded by
  /// |BeginRendering()|.
  void EndRendering();

  /// Submits the render data buffers to be processed for rendering. Note this
  /// doesn't actually render the data, only makes the buffers ready to be
  /// processed by the render functions.
  void SubmitRenderData();

  /// Executes zero or one deferred tasks per call. Should be called once per
  /// frame.
  void ProcessTasks();

  /// Renders all objects in |views| for each predefined render pass.
  void Render(const RenderView* views, size_t num_views);

  /// Renders all objects in |views| for the specified |pass|.
  void Render(const RenderView* views, size_t num_views, HashValue pass);

  /// Sets |pass|'s clear params.
  void SetClearParams(HashValue pass, const RenderClearParams& clear_params);

  /// Sets the render system to draw in stereoscopic multi view mode.
  void SetStereoMultiviewEnabled(bool enabled);

  /// Sets |pass|'s sort mode.
  void SetSortMode(HashValue pass, SortMode mode);

  /// Sets |pass|'s cull mode.
  void SetCullMode(HashValue pass, RenderCullMode mode);

  /// Sets the default winding / front face to use. Default is
  /// kCounterClockwise, same as OpenGL.
  void SetDefaultFrontFace(RenderFrontFace face);

  /// Sets depth function to kDepthFunctionLess if |enabled| and to
  /// kDepthFunctionDisabled if !|enabled| in RenderSystemFpl.
  /// Sets kDepthTest to |enabled| in RenderSystemIon.
  void SetDepthTest(bool enabled);

  /// Sets glDepthMask to |enabled|.
  void SetDepthWrite(bool enabled);

  /// Sets the GL blend mode to |blend_mode|.
  void SetBlendMode(const fplbase::BlendMode blend_mode);

  /// Sets |view| to be the screen rectangle that gets rendered to.
  void SetViewport(const RenderView& view);

  /// Sets |mvp| to be a position at which to project models.
  void SetClipFromModelMatrix(const mathfu::mat4& mvp);

  /// Sets the render target to be used when rendering a specific pass.
  void SetRenderTarget(HashValue pass, HashValue render_target_name);

  /// Creates a render target that can be used in a pass for rendering, and as a
  /// texture on top of an object.
  void CreateRenderTarget(HashValue render_target_name,
                          const RenderTargetCreateParams& create_params);

  /// Sets a render state to be used when rendering a specific render pass. If
  /// a pass is rendered without a state being set, a default render state will
  /// be used.
  void SetRenderState(HashValue pass, const fplbase::RenderState& render_state);

  /// Sets the function to use for calculating the clip_from_model_matrix value
  /// (i.e. the model-view-projection). This will override the default
  /// calculation which simply multiples the projection_view by the model
  /// matrix. Providing nullptr will reset the RenderSystem to using the default
  /// function. This is applied globally for all render passes and all entities.
  using ClipFromModelMatrixFn =
      std::function<mathfu::mat4(const mathfu::mat4&, const mathfu::mat4&)>;
  void SetClipFromModelMatrixFunction(const ClipFromModelMatrixFn& func);

  /// Create and return a pre-processed texture.  This will set up a rendering
  /// environment suitable to render |source_texture| with a pre-process shader.
  /// Texture and shader binding / setup should be performed in |processor|.
  using TextureProcessor = std::function<void(TexturePtr)>;
  TexturePtr CreateProcessedTexture(const TexturePtr& source_texture,
                                    bool create_mips,
                                    TextureProcessor processor);
  TexturePtr CreateProcessedTexture(const TexturePtr& source_texture,
                                    bool create_mips,
                                    RenderSystem::TextureProcessor processor,
                                    const mathfu::vec2i& output_dimensions);

  /// Immediately binds |shader|.
  void BindShader(const ShaderPtr& shader);

  /// Immediately binds |texture| in |unit|.
  void BindTexture(int unit, const TexturePtr& texture);

  /// Immediately binds |uniform| on the currently bound shader.
  void BindUniform(const char* name, const float* data, int dimension);

  /// Bind a uniform to the currently bound shader.
  void BindUniform(const Uniform& uniform);

  /// Immediately draws |mesh|.
  void DrawMesh(const MeshData& mesh);

  /// Returns a resident white texture with an alpha channel: (1, 1, 1, 1).
  const TexturePtr& GetWhiteTexture() const;

  /// Returns a resident invalid texture to be used when a requested image fails
  /// to load.  On debug builds it's a watermelon; on release builds it's just
  /// the white texture.
  const TexturePtr& GetInvalidTexture() const;

  /// Adds a mesh and corresponding rendering information with the Entity using
  /// the specified ComponentDef.
  void Create(Entity entity, HashValue type, const Def* def) override;

  /// Creates an empty render component for an Entity. It is expected to be
  /// populated in code.
  void Create(Entity entity, HashValue pass);

  /// Performs post creation initialization.
  void PostCreateInit(Entity entity, HashValue type, const Def* def) override;

  /// Disassociates all rendering data from the Entity.
  void Destroy(Entity entity) override;

  /// Disassociates all rendering data identified by |pass| from the Entity.
  void Destroy(Entity entity, HashValue pass);

  /// Stops rendering the entity.
  void Hide(Entity entity);

  /// Resumes rendering the entity.
  void Show(Entity entity);

  /// Sets |e|'s render pass to |pass|.
  void SetRenderPass(Entity entity, HashValue pass);

  /// Returns |entity|'s render pass, or RenderPass_Invalid if |entity| isn't
  /// known to RenderSystem.
  HashValue GetRenderPass(Entity entity) const;

  /// Returns |entity|'s default color, as specified in its json.
  const mathfu::vec4& GetDefaultColor(Entity entity) const;

  /// Sets the |entity|'s default color, overriding the color specified in its
  /// json.
  void SetDefaultColor(Entity entity, const mathfu::vec4& color);

  /// Copies the cached value of |entity|'s color uniform into |color|. Returns
  /// false if the value of the uniform was not found.
  bool GetColor(Entity entity, mathfu::vec4* color) const;

  /// Sets the shader's color uniform for the specified |entity|.
  void SetColor(Entity entity, const mathfu::vec4& color);

  /// Sets a shader uniform value for the specified Entity for all passes.  The
  /// |dimension| must be 1, 2, 3, 4, or 16. Arrays of vector with dimension 2
  /// or 3 should contain vec2_packed or vec3_packed. The size of the |data|
  /// array is assumed to be the same as |dimension|.
  void SetUniform(Entity entity, const char* name, const float* data,
                  int dimension);

  /// Sets an array of shader uniform values for the specified Entity for all
  /// passes.  The |dimension| must be 1, 2, 3, 4, or 16. Arrays of vector with
  /// dimension 2 or 3 should contain vec2_packed or vec3_packed.  The size of
  /// the |data| array is assumed to be the same as |dimension| * |count|.
  void SetUniform(Entity entity, const char* name, const float* data,
                  int dimension, int count);

  /// Sets an array of shader uniform values for the specified Entity identified
  /// via |pass|.  The |dimension| must be 1, 2, 3, 4, or 16. Arrays of vector
  /// with dimension 2 or 3 should contain vec2_packed or vec3_packed.  The size
  /// of the |data| array is assumed to be the same as |dimension| * |count|.
  void SetUniform(Entity entity, HashValue pass, const char* name,
                  const float* data, int dimension, int count);

  /// Copies the cached value of the uniform |name| into |data_out|, respecting
  /// the |length| limit. Returns false if the value of the uniform was not
  /// found.
  bool GetUniform(Entity entity, const char* name, size_t length,
                  float* data_out) const;

  /// Copies an |entity|'s (associated with an |pass|) cached value of the
  /// uniform |name| into |data_out|, respecting the |length| limit. Returns
  /// false if the value of the uniform was not found.
  bool GetUniform(Entity entity, HashValue pass, const char* name,
                  size_t length, float* data_out) const;

  /// Makes |entity| use all the same uniform values as |source|.
  void CopyUniforms(Entity entity, Entity source);

  /// Attaches a texture to the specified Entity for all passes.
  void SetTexture(Entity entity, int unit, const TexturePtr& texture);

  /// Attaches a texture to the specified Entity for the specified pass.
  void SetTexture(Entity entity, HashValue pass, int unit,
                  const TexturePtr& texture);

  /// Loads and attaches a texture to the specified Entity for all passes.
  void SetTexture(Entity entity, int unit, const std::string& file);

  /// Attaches a texture object with given GL |texture_target| and |texture_id|
  /// to the specified Entity for all passes.
  void SetTextureId(Entity entity, int unit, uint32_t texture_target,
                    uint32_t texture_id);
  void SetTextureId(Entity entity, HashValue pass, int unit,
                    uint32_t texture_target, uint32_t texture_id);

  /// Returns a pointer to the texture assigned to |entity|'s |unit|.
  TexturePtr GetTexture(Entity entity, int unit) const;

  /// Retrieves a mesh attached to the specified |entity| identified by
  /// |pass|.
  MeshPtr GetMesh(Entity entity, HashValue pass);

  /// Attaches a mesh to the specified |entity| identified by |pass|. Does
  /// not apply deformation.
  void SetMesh(Entity entity, HashValue pass, const MeshPtr& mesh);

  /// Attaches a mesh to the specified Entity for all passes. Does not apply
  /// deformation.
  void SetMesh(Entity entity, const MeshData& mesh);

  /// Like SetMesh, but applies |entity|'s deformation, if any. If |mesh|
  /// doesn't have read access, this will generate a warning and not apply the
  /// deformation.
  void SetAndDeformMesh(Entity entity, const MeshData& mesh);

  /// Loads and attaches a mesh to the specified Entity for all passes.
  void SetMesh(Entity entity, const std::string& file);

  /// Sets the material (which is a combination of shaders, textures, render
  /// state, etc.) on the specified Entity.
  void SetMaterial(Entity entity, int submesh_index, const MaterialInfo& info);

  /// Returns |entity|'s sort order.
  RenderSortOrder GetSortOrder(Entity entity) const;

  /// Returns |e|'s sort order offset.
  RenderSortOrderOffset GetSortOrderOffset(Entity entity) const;

  /// Sets the offset used when determining this Entity's draw order.
  void SetSortOrderOffset(Entity entity,
                          RenderSortOrderOffset sort_order_offset);
  void SetSortOrderOffset(Entity entity, HashValue pass,
                          RenderSortOrderOffset sort_order_offset);

  /// Defines an entity's stencil mode.
  void SetStencilMode(Entity entity, RenderStencilMode mode, int value);

  /// Returns whether or not a texture unit has a texture for an entity.
  bool IsTextureSet(Entity entity, int unit) const;

  /// Returns whether or not a texture unit is ready to render.
  bool IsTextureLoaded(Entity entity, int unit) const;

  /// Returns whether or not the texture has been loaded.
  bool IsTextureLoaded(const TexturePtr& texture) const;

  /// Returns true if all currently set assets have loaded.
  bool IsReadyToRender(Entity entity) const;

  /// Returns whether or not |e| is hidden / rendering. If the entity is not
  /// registered with the RenderSystem, this will return true.
  bool IsHidden(Entity entity) const;

  /// Specifies custom deformation function for dynamically generated meshes.
  /// This function must be set prior to entity creation. If it is then the
  /// render system will defer generating the deformed mesh until the first call
  /// to ProcessTasks. Updates to this function will not affect any previously
  /// generated meshes, but future calls to SetText and SetQuad will use the new
  /// deformation function.
  using DeformationFn = std::function<void(MeshData* mesh)>;
  void SetDeformationFunction(Entity entity, const DeformationFn& deform);

  /// Creates a temporary interface that allows a mesh to be defined for
  /// |entity|.  This mesh is used until UpdateDynamicMesh is called again.
  //
  /// This function will locate or create a suitable mesh object that matches
  /// the requested primitive, vertex and index parameters.  This mesh may have
  /// restrictions on it for performance reasons, or may have been previously
  /// used for a different purpose, so you should not make any assumptions about
  /// the readability or contents of this mesh; instead, you should only write
  /// your data into it.  Note also that due to these restrictions, deformation
  /// will not be performed on the mesh.
  void UpdateDynamicMesh(Entity entity, MeshData::PrimitiveType primitive_type,
                         const VertexFormat& vertex_format,
                         const size_t max_vertices, const size_t max_indices,
                         const std::function<void(MeshData*)>& update_mesh);

  /// Returns the underlyng RenderSystemImpl (eg. RenderSystemFpl,
  /// RenderSystemIon) to expose implementation-specific behaviour depending on
  /// which implementation is depended upon. The RenderSystemImpl header which
  /// is used must match the render system that is depended upon in the BUILD
  /// rule.
  RenderSystemImpl* GetImpl();

  /// IMPORTANT: The following legacy functions are deprecated.

  /// DEPRECATED: use Create(entity, pass) instead.
  /// Creates an empty render component for an Entity identified by |pass|.
  void Create(Entity entity, HashValue pass, HashValue pass_enum);

  /// DEPRECATED: Waits for all outstanding rendering assets to finish loading.
  void WaitForAssetsToLoad();

  /// DEPRECATED. Sets the value used to clear the color buffer.
  void SetClearColor(float r, float g, float b, float a);

  /// DEPRECATED. Returns the cached value of the clear color.
  mathfu::vec4 GetClearColor() const;

  /// DEPRECATED: Only for fpl and ion; others should use the TextureFactory.
  /// Loads the texture with the given |filename|. When using the
  /// next backend, the render system holds only a weak reference to the
  /// texture, so it will be automatically unloaded when it falls out of use.
  TexturePtr LoadTexture(const std::string& filename);

  /// DEPRECATED: Only for fpl and ion; others should use the TextureFactory.
  /// Loads the texture with the given |filename| and optionally creates mips.
  /// When using the next backend, the render system holds only a weak reference
  /// to the texture, so it will be automatically unloaded when it falls out of
  /// use.
  TexturePtr LoadTexture(const std::string& filename, bool create_mips);

  /// DEPRECATED: Loads the texture atlas with the given |filename|. The render
  /// system maintains a reference to all atlases.
  void LoadTextureAtlas(const std::string& filename);

  /// DEPRECATED: Only for fpl and ion; others should use the TextureFactory.
  /// Returns a texture that had been loaded by its hash. If the texture doesn't
  /// exist this will return |nullptr|.
  TexturePtr GetTexture(HashValue texture_hash) const;

  /// DEPRECATED: Only for fpl and ion; others should use the TextureFactory.
  /// Creates a texture from image data.
  TexturePtr CreateTexture(const ImageData& image);

  /// DEPRECATED: Only for fpl and ion; others should use the TextureFactory.
  /// Creates a texture from image data and optionally creates mips.
  TexturePtr CreateTexture(const ImageData& image, bool create_mips);

  /// DEPRECATED: Only for fpl and ion; others should use SetMaterial.
  /// Loads the shader with the given |filename|.
  ShaderPtr LoadShader(const std::string& filename);

  /// DEPRECATED: Only for fpl and ion; others should use SetMaterial.
  /// Returns |entity|'s shader, or nullptr if it isn't known to RenderSystem.
  ShaderPtr GetShader(Entity entity, HashValue pass) const;

  /// DEPRECATED: Only for fpl and ion; others should use SetMaterial.
  /// Returns |entity|'s shader, or nullptr if it isn't known to RenderSystem.
  ShaderPtr GetShader(Entity entity) const;

  /// DEPRECATED: Only for fpl and ion; others should use SetMaterial.
  /// Attaches a shader program to specified |entity| identified by |pass|.
  void SetShader(Entity entity, HashValue pass, const ShaderPtr& shader);

  /// DEPRECATED: Only for fpl and ion; others should use SetMaterial.
  /// Attaches a shader program to |entity|.
  void SetShader(Entity entity, const ShaderPtr& shader);

  /// DEPRECATED: Only for fpl and ion; others should use SetMaterial.
  /// Loads and attaches a shader to the specified Entity.
  void SetShader(Entity entity, const std::string& file);

  /// DEPRECATED: Only for fpl and ion; others should use the MeshFactory.
  /// Loads a mesh with the given filename. When using the next backend the
  /// render system holds a weak reference to the Mesh, otherwise it is a strong
  /// reference.
  MeshPtr LoadMesh(const std::string& filename);

  /// DEPRECATED: Loads a font.
  void PreloadFont(const char* name);

  /// DEPRECATED.
  /// FPL: Loads a list of fonts.  Each glyph will check each font in the list
  /// until it finds one that supports it.
  /// Ion: Only the first font is loaded.
  FontPtr LoadFonts(const std::vector<std::string>& names);

  /// DEPRECATED: Updates the entity to display a text string. If there is a
  /// deformation function set on this entity then the quad generation will be
  /// deferred until ProcessTasks is called.
  void SetText(Entity entity, const std::string& text);

  /// DEPRECATED: Sets |font| on |entity|.  Takes effect on the next call to
  /// SetText.
  void SetFont(Entity entity, const FontPtr& font);

  /// DEPRECATED: Sets |entity|'s text size to |size|, which measures mm between
  /// lines.
  void SetTextSize(Entity entity, int size);

  /// DEPRECATED: Copies the cached value of the |entity|'s Quad into |quad|.
  /// Returns false if no quad is found.
  bool GetQuad(Entity entity, RenderQuad* quad) const;

  /// DEPRECATED: Creates a Quad of a given size. If there is a deformation
  /// function set on this entity then the quad generation will be deferred
  /// until ProcessTasks is called.
  void SetQuad(Entity entity, const RenderQuad& quad);
  void SetQuad(Entity entity, HashValue pass, const RenderQuad& quad);

  /// DEPRECATED: Returns the number of bones associated with |entity|.
  int GetNumBones(Entity entity) const;

  /// DEPRECATED: Returns the array of bone indices associated with the Entity
  /// |e| used for skeletal animations.
  const uint8_t* GetBoneParents(Entity entity, int* num) const;

  /// DEPRECATED: Returns the array of bone names associated with the Entity
  /// |e|. The length of the array is GetNumBones(e), and 'num' will be set to
  /// this if non-null.
  const std::string* GetBoneNames(Entity entity, int* num) const;

  /// DEPRECATED: Returns the array of default bone transform inverses (AKA
  /// inverse bind-pose matrices) associated with the Entity |e|.  The length of
  /// the array is GetNumBones(e), and 'num' will be set to this if non-null.
  const mathfu::AffineTransform* GetDefaultBoneTransformInverses(
      Entity entity, int* num) const;

  /// DEPRECATED: Sets |entity|'s shader uniforms using |transforms|.
  void SetBoneTransforms(Entity entity,
                         const mathfu::AffineTransform* transforms,
                         int num_transforms);

  /// DEPRECATED: use DrawMesh instead.
  /// Immediately draws a list of vertices arranged by |primitive_type|.
  void DrawPrimitives(MeshData::PrimitiveType primitive_type,
                      const VertexFormat& vertex_format,
                      const void* vertex_data, size_t num_vertices);

  template <typename Vertex>
  void DrawPrimitives(MeshData::PrimitiveType primitive_type,
                      const Vertex* vertices, size_t num_vertices);

  template <typename Vertex>
  void DrawPrimitives(MeshData::PrimitiveType primitive_type,
                      const std::vector<Vertex>& vertices);

  /// DEPRECATED: use DrawMesh instead.
  /// Immediately draws an indexed list of vertices arranged by
  /// |primitive_type|.
  void DrawIndexedPrimitives(MeshData::PrimitiveType primitive_type,
                             const VertexFormat& vertex_format,
                             const void* vertex_data, size_t num_vertices,
                             const uint16_t* indices, size_t num_indices);

  template <typename Vertex>
  void DrawIndexedPrimitives(MeshData::PrimitiveType primitive_type,
                             const Vertex* vertices, size_t num_vertices,
                             const uint16_t* indices, size_t num_indices);

  template <typename Vertex>
  void DrawIndexedPrimitives(MeshData::PrimitiveType primitive_type,
                             const std::vector<Vertex>& vertices,
                             const std::vector<uint16_t>& indices);

  /// DEPRECATED. Returns the render state cached by the renderer.
  const fplbase::RenderState& GetCachedRenderState() const;

  /// DEPRECATED. Updates the render state cached in the renderer. This should
  /// be used if your app is sharing a GL context with another framework which
  /// affects the GL state, or if you are making GL calls on your own outside of
  /// Lullaby.
  void UpdateCachedRenderState(const fplbase::RenderState& render_state);

  /// BEGIN-GOOGLE-INTERNAL
  /// Loads and sets the pano on the entity.
  void SetPano(Entity entity, const std::string& filename,
               float heading_offset_deg);

  /// DEPRECATED. Type aliases for backwards compatibility.
  using Deformation = DeformationFn;
  using CalculateClipFromModelMatrixFunc = ClipFromModelMatrixFn;
  using CullMode = RenderCullMode;
  using FrontFace = RenderFrontFace;
  using StencilMode = RenderStencilMode;
  using PrimitiveType = MeshData::PrimitiveType;
  using Quad = RenderQuad;
  using SortOrder = RenderSortOrder;
  using SortOrderOffset = RenderSortOrderOffset;
  using View = RenderView;
  using ClearParams = RenderClearParams;

 private:
  std::unique_ptr<RenderSystemImpl> impl_;
};

template <typename Vertex>
inline void RenderSystem::DrawPrimitives(MeshData::PrimitiveType primitive_type,
                                         const Vertex* vertices,
                                         size_t num_vertices) {
  DrawPrimitives(primitive_type, Vertex::kFormat, vertices, num_vertices);
}

template <typename Vertex>
inline void RenderSystem::DrawPrimitives(MeshData::PrimitiveType primitive_type,
                                         const std::vector<Vertex>& vertices) {
  DrawPrimitives(primitive_type, vertices.data(), vertices.size());
}

template <typename Vertex>
inline void RenderSystem::DrawIndexedPrimitives(
    MeshData::PrimitiveType primitive_type, const Vertex* vertices,
    size_t num_vertices, const uint16_t* indices, size_t num_indices) {
  DrawIndexedPrimitives(primitive_type, Vertex::kFormat, vertices, num_vertices,
                        indices, num_indices);
}

template <typename Vertex>
inline void RenderSystem::DrawIndexedPrimitives(
    MeshData::PrimitiveType primitive_type, const std::vector<Vertex>& vertices,
    const std::vector<uint16_t>& indices) {
  DrawIndexedPrimitives(primitive_type, Vertex::kFormat, vertices.data(),
                        vertices.size(), indices.data(), indices.size());
}

}  // namespace lull

LULLABY_SETUP_TYPEID(lull::RenderSystem);

#endif  // LULLABY_SYSTEMS_RENDER_RENDER_SYSTEM_H_
