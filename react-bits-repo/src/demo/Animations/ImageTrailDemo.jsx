import { useMemo } from 'react';
import { CodeTab, PreviewTab, TabsLayout } from '../../components/common/TabsLayout';
import { Box, Button, ButtonGroup, Flex, Text } from '@chakra-ui/react';

import CodeExample from '../../components/code/CodeExample';

import PropTable from '../../components/common/Preview/PropTable';
import Dependencies from '../../components/code/Dependencies';
import useForceRerender from '../../hooks/useForceRerender';
import useComponentProps from '../../hooks/useComponentProps';
import { ComponentPropsProvider } from '../../components/context/ComponentPropsContext';

import OpenInStudioButton from '../../components/common/Preview/OpenInStudioButton';

import { imageTrail } from '../../constants/code/Animations/imageTrailCode';
import ImageTrail from '../../content/Animations/ImageTrail/ImageTrail';

const DEFAULT_PROPS = {
  variant: '1'
};

const ImageTrailDemo = () => {
  const { props, updateProp, resetProps, hasChanges } = useComponentProps(DEFAULT_PROPS);
  const { variant } = props;
  const [key, forceRerender] = useForceRerender();

  const propData = useMemo(
    () => [
      {
        name: 'items',
        type: 'string[]',
        default: '[]',
        description: 'An array of image URLs which will be animated in the trail.'
      },
      {
        name: 'variant',
        type: 'number',
        default: '1',
        description: 'A number from 1 to 8 - all different animation styles.'
      }
    ],
    []
  );

  return (
    <ComponentPropsProvider props={props} defaultProps={DEFAULT_PROPS} resetProps={resetProps} hasChanges={hasChanges}>
      <TabsLayout>
        <PreviewTab>
          <Box position="relative" className="demo-container" h={400} overflow="hidden">
            <ImageTrail
              key={key}
              items={[
                'https://picsum.photos/id/287/300/300',
                'https://picsum.photos/id/1001/300/300',
                'https://picsum.photos/id/1025/300/300',
                'https://picsum.photos/id/1026/300/300',
                'https://picsum.photos/id/1027/300/300',
                'https://picsum.photos/id/1028/300/300',
                'https://picsum.photos/id/1029/300/300',
                'https://picsum.photos/id/1030/300/300'
              ]}
              variant={variant}
            />

            <Flex position="absolute" justifyContent="center" flexDirection="column" alignItems="center">
              <Text fontSize="clamp(2rem, 6vw, 6rem)" fontWeight={900} color="#271E37" mb={0}>
                Hover Me.
              </Text>
              <Text fontSize="18px" fontWeight={900} color="#a6a6a6" mt={0}>
                Variant {variant}
              </Text>
            </Flex>
          </Box>

          <Flex justify="flex-end" mt={2} mb={-2}>
            <OpenInStudioButton backgroundId="image-trail" currentProps={{ variant }} defaultProps={DEFAULT_PROPS} />
          </Flex>

          <div className="preview-options">
            <h2 className="demo-title-extra">Customize</h2>
            <Flex gap={6} direction="column">
              <ButtonGroup isAttached size="sm">
                <Button
                  fontSize="xs"
                  disabled
                  border="1px solid #271E37"
                  h={8}
                  _disabled={{
                    bg: '#271E37',
                    border: '1px solid #271E37',
                    color: '#fff',
                    cursor: 'not-allowed',
                    _hover: { bg: '#222' }
                  }}
                >
                  Variant
                </Button>
                {[1, 2, 3, 4, 5, 6, 7, 8].map(num => {
                  const isActive = variant === String(num);

                  return (
                    <Button
                      key={num}
                      bg={isActive ? '#5227FF' : '#0D0716'}
                      border="1px solid #271E37"
                      _hover={{ backgroundColor: isActive ? '#5227FF' : '#0D0716' }}
                      color="#fff"
                      fontSize="xs"
                      h={8}
                      onClick={() => {
                        updateProp('variant', String(num));
                        forceRerender();
                      }}
                    >
                      {num}
                    </Button>
                  );
                })}
              </ButtonGroup>
            </Flex>
          </div>

          <PropTable data={propData} />
          <Dependencies dependencyList={['gsap']} />
        </PreviewTab>

        <CodeTab>
          <CodeExample codeObject={imageTrail} componentName="ImageTrail" />
        </CodeTab>
      </TabsLayout>
    </ComponentPropsProvider>
  );
};

export default ImageTrailDemo;
