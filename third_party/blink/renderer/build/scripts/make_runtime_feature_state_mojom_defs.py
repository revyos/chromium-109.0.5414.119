import json5_generator
import make_runtime_features
import make_runtime_features_utilities as util
import template_expander


class RuntimeFeatureStateMojomWriter(
        make_runtime_features.BaseRuntimeFeatureWriter):
    file_basename = "runtime_feature_state"

    def __init__(self, json5_file_path, output_dir):
        super(RuntimeFeatureStateMojomWriter,
              self).__init__(json5_file_path, output_dir)
        self._outputs = {
            (self.file_basename + '.mojom'): self.generate_mojom_definition
        }
        self._browser_read_access_features = util.browser_read_access(
            self._features)
        self._browser_write_access_features = util.browser_write_access(
            self._features)

    def _template_inputs(self):
        return {
            'features': self._features,
            'browser_read_access_features': self._browser_read_access_features,
            'browser_write_access_features':
            self._browser_write_access_features,
            'platforms': self._platforms(),
            'input_files': self._input_files,
            'header_guard': self._header_guard,
        }

    @template_expander.use_jinja(f'templates/{file_basename}.mojom.tmpl')
    def generate_mojom_definition(self):
        return {
            'browser_read_access_features': self._browser_read_access_features,
            'browser_write_access_features':
            self._browser_write_access_features,
            'input_files': self._input_files,
        }


if __name__ == '__main__':
    json5_generator.Maker(RuntimeFeatureStateMojomWriter).main()
