import { useMemo } from 'react';
import { CodeTab, PreviewTab, TabsLayout } from '../../components/common/TabsLayout';
import { Box, Flex, Input, Text } from '@chakra-ui/react';

import Customize from '../../components/common/Preview/Customize';
import CodeExample from '../../components/code/CodeExample';

import PropTable from '../../components/common/Preview/PropTable';
import Dependencies from '../../components/code/Dependencies';
import PreviewSlider from '../../components/common/Preview/PreviewSlider';
import PreviewSwitch from '../../components/common/Preview/PreviewSwitch';
import PreviewSelect from '../../components/common/Preview/PreviewSelect';
import OpenInStudioButton from '../../components/common/Preview/OpenInStudioButton';

import useForceRerender from '../../hooks/useForceRerender';
import useComponentProps from '../../hooks/useComponentProps';
import { ComponentPropsProvider } from '../../components/context/ComponentPropsContext';

import { plasma } from '../../constants/code/Backgrounds/plasmaCode';
import Plasma from '../../ts-default/Backgrounds/Plasma/Plasma';
import BackgroundContent from '../../components/common/Preview/BackgroundContent';

const DEFAULT_PROPS = {
  color: '#B19EEF',
  speed: 1.0,
  direction: 'forward',
  scale: 1.0,
  opacity: 1.0,
  mouseInteractive: false
};

const PlasmaDemo = () => {
  const { props, updateProp, resetProps, hasChanges } = useComponentProps(DEFAULT_PROPS);
  const { color, speed, direction, scale, opacity, mouseInteractive } = props;
  const [key, forceRerender] = useForceRerender();

  const propData = useMemo(
    () => [
      {
        name: 'color',
        type: 'string',
        default: 'undefined',
        description: 'Optional hex color to tint the plasma effect. If not provided, uses original colors.'
      },
      {
        name: 'speed',
        type: 'number',
        default: '1.0',
        description: 'Animation speed multiplier. Higher values = faster animation.'
      },
      {
        name: 'direction',
        type: "'forward' | 'reverse' | 'pingpong'",
        default: "'forward'",
        description: "Animation direction. 'pingpong' oscillates back and forth."
      },
      {
        name: 'scale',
        type: 'number',
        default: '1.0',
        description: 'Zoom level of the plasma pattern. Higher values zoom in.'
      },
      {
        name: 'opacity',
        type: 'number',
        default: '1.0',
        description: 'Overall opacity of the effect (0-1).'
      },
      {
        name: 'mouseInteractive',
        type: 'boolean',
        default: 'false',
        description: 'Whether the plasma responds to mouse movement.'
      }
    ],
    []
  );

  return (
    <ComponentPropsProvider resetProps={resetProps} hasChanges={hasChanges} forceRerender={forceRerender}>
      <TabsLayout>
        <PreviewTab>
          <Box position="relative" className="demo-container" h={600} p={0} overflow="hidden">
            <Plasma
              key={key}
              color={color}
              speed={speed}
              direction={direction}
              scale={scale}
              opacity={opacity}
              mouseInteractive={mouseInteractive}
            />
            <BackgroundContent pillText="New Background" headline="Minimal plasma waves that soothe the eyes" />
          </Box>

          <Flex justify="flex-end" mt={2} mb={-2}>
            <OpenInStudioButton
              backgroundId="plasma"
              currentProps={{ color, speed, scale, opacity, mouseInteractive }}
              defaultProps={{ color: '#B19EEF', speed: 1.0, scale: 1.0, opacity: 1.0, mouseInteractive: false }}
            />
          </Flex>

          <Customize>
            <Flex alignItems="center" mb={4}>
              <Text fontSize="sm" mr={2}>
                Color
              </Text>
              <Input
                type="color"
                value={color}
                onChange={e => {
                  updateProp('color', e.target.value);
                }}
                width="50px"
              />
            </Flex>

            <PreviewSelect
              title="Direction"
              options={[
                { value: 'forward', label: 'Forward' },
                { value: 'reverse', label: 'Reverse' },
                { value: 'pingpong', label: 'Ping Pong' }
              ]}
              value={direction}
              onChange={val => updateProp('direction', val)}
              width={120}
            />

            <PreviewSlider
              title="Speed"
              min={0.1}
              max={3.0}
              step={0.1}
              value={speed}
              onChange={val => updateProp('speed', val)}
            />

            <PreviewSlider
              title="Scale"
              min={0.5}
              max={3.0}
              step={0.1}
              value={scale}
              onChange={val => updateProp('scale', val)}
            />

            <PreviewSlider
              title="Opacity"
              min={0.1}
              max={1.0}
              step={0.1}
              value={opacity}
              onChange={val => updateProp('opacity', val)}
            />

            <PreviewSwitch
              title="Mouse Interactive"
              isChecked={mouseInteractive}
              onChange={val => updateProp('mouseInteractive', val)}
            />
          </Customize>

          <PropTable data={propData} />
          <Dependencies dependencyList={['ogl']} />
        </PreviewTab>

        <CodeTab>
          <CodeExample codeObject={plasma} componentName="Plasma" />
        </CodeTab>
      </TabsLayout>
    </ComponentPropsProvider>
  );
};

export default PlasmaDemo;
